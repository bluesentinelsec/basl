/* VIGIL standard library: http module.
 *
 * HTTP/S client using OS-native libraries loaded at runtime:
 *   - WinHTTP via LoadLibrary() on Windows
 *   - libcurl via dlopen() on POSIX
 * Fallback to plain TCP socket-based HTTP/1.1 client and server (no TLS).
 *
 * All platform operations go through platform.h — no OS headers here.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/runtime.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"
#include "platform/platform.h"

/* ── Test visibility ─────────────────────────────────────────────── */

#ifdef VIGIL_HTTP_TESTING
#define HTTP_STATIC VIGIL_API
#else
#define HTTP_STATIC static
#endif

/* ── Allocator helpers ───────────────────────────────────────────── */

static vigil_allocator_t get_alloc(vigil_vm_t *vm)
{
    const vigil_allocator_t *a = vigil_runtime_allocator(vigil_vm_runtime(vm));
    if (a != NULL)
        return *a;
    return vigil_default_allocator();
}

/*
 * File-level allocator for internal helpers that lack vm access.
 * Set by each native entry point before calling internal helpers.
 * The http module already uses extensive file-level global state
 * (g_servers, g_clients, etc.) so this fits the existing pattern.
 */
static vigil_allocator_t g_http_alloc;
static int g_http_alloc_set = 0;

static void *http_alloc(size_t size)
{
    if (g_http_alloc_set)
        return g_http_alloc.allocate(g_http_alloc.user_data, size);
    return malloc(size);
}

static void *http_realloc(void *ptr, size_t size)
{
    if (g_http_alloc_set && g_http_alloc.reallocate)
        return g_http_alloc.reallocate(g_http_alloc.user_data, ptr, size);
    return realloc(ptr, size);
}

static void http_dealloc(void *ptr)
{
    if (g_http_alloc_set)
        g_http_alloc.deallocate(g_http_alloc.user_data, ptr);
    else
        free(ptr);
}

/* ── VM helpers ──────────────────────────────────────────────────── */

static vigil_status_t push_string(vigil_vm_t *vm, const char *s, size_t len, vigil_error_t *error)
{
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_object_t *obj = NULL;
    vigil_status_t st = vigil_string_object_new(rt, s, len, &obj, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_t val;
    vigil_value_init_object(&val, &obj);
    st = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return st;
}

static vigil_status_t push_i64(vigil_vm_t *vm, int64_t v, vigil_error_t *error)
{
    vigil_value_t val = vigil_nanbox_encode_int(v);
    return vigil_vm_stack_push(vm, &val, error);
}

static int get_string_arg(vigil_vm_t *vm, size_t base, size_t idx, const char **out, size_t *out_len)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    const vigil_object_t *obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(v);
    if (!obj || vigil_object_type(obj) != VIGIL_OBJECT_STRING)
        return 0;
    *out = vigil_string_object_c_str(obj);
    *out_len = vigil_string_object_length(obj);
    return 1;
}

static int64_t get_i64_arg(vigil_vm_t *vm, size_t base, size_t idx)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    if (vigil_nanbox_is_int(v))
        return vigil_nanbox_decode_int(v);
    return 0;
}

/* ── URL parsing ─────────────────────────────────────────────────── */

typedef struct
{
    char scheme[16];
    char host[256];
    int port;
    char path[2048];
} parsed_url_t;

static void str_tolower(char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
    {
        if (s[i] >= 'A' && s[i] <= 'Z')
            s[i] = (char)(s[i] + 32);
    }
}

HTTP_STATIC int parse_url(const char *url, parsed_url_t *out)
{
    memset(out, 0, sizeof(*out));
    memcpy(out->path, "/", 2);

    const char *p = url;
    const char *scheme_end = strstr(p, "://");
    if (scheme_end)
    {
        size_t slen = (size_t)(scheme_end - p);
        if (slen >= sizeof(out->scheme))
            return 0;
        memcpy(out->scheme, p, slen);
        str_tolower(out->scheme, slen); /* normalise to lowercase */
        p = scheme_end + 3;
    }
    else
    {
        memcpy(out->scheme, "http", 5);
    }

    const char *path_start = strchr(p, '/');
    const char *port_start = strchr(p, ':');
    const char *host_end = path_start ? path_start : p + strlen(p);
    if (port_start && port_start < host_end)
        host_end = port_start;

    size_t hlen = (size_t)(host_end - p);
    if (hlen >= sizeof(out->host))
        return 0;
    memcpy(out->host, p, hlen);

    if (port_start && port_start < (path_start ? path_start : p + strlen(p)))
    {
        out->port = atoi(port_start + 1);
    }
    else
    {
        out->port = (strcmp(out->scheme, "https") == 0) ? 443 : 80;
    }

    if (path_start)
    {
        size_t plen = strlen(path_start);
        if (plen >= sizeof(out->path))
            return 0; /* path too long — fail rather than silently truncate */
        memcpy(out->path, path_start, plen);
        out->path[plen] = '\0';
    }
    return 1;
}

/* ── Response structure ──────────────────────────────────────────── */

typedef struct
{
    int status_code;
    char *headers;
    char *body;
    size_t body_len;
} http_response_t;

HTTP_STATIC void response_free(http_response_t *r)
{
    http_dealloc(r->headers);
    http_dealloc(r->body);
    memset(r, 0, sizeof(*r));
}

/* ── Socket-based HTTP/1.1 fallback (no TLS) ─────────────────────── */

#define HTTP_MAX_REQUEST_OVERHEAD 80U /* method+path+host+fixed headers margin */
#define HTTP_MAX_HEADER_BYTES (64U * 1024U)
#define HTTP_MAX_BODY_BYTES (8U * 1024U * 1024U)
#define HTTP_MAX_RESPONSE_HEADER_BYTES (256U * 1024U)
#define HTTP_MAX_RESPONSE_BODY_BYTES (64U * 1024U * 1024U)
#define HTTP_DEFAULT_TIMEOUT_MS 30000

static int tcp_send_all(vigil_socket_t sock, const void *data, size_t len)
{
    const char *cursor = (const char *)data;
    size_t sent_total = 0;

    while (sent_total < len)
    {
        size_t sent_now = 0;
        vigil_status_t st = vigil_platform_tcp_send(sock, cursor + sent_total, len - sent_total, &sent_now, NULL);
        if (st != VIGIL_STATUS_OK || sent_now == 0)
        {
            return -1;
        }
        sent_total += sent_now;
    }

    return 0;
}

static char *alloc_request_buf(const char *method, const char *path, const char *host, const char *headers,
                               size_t body_len, size_t *out_len)
{
    size_t cap = strlen(method) + 1 + strlen(path) + 11 + 7 + strlen(host) + 2 + 19 + (headers ? strlen(headers) : 0) +
                 HTTP_MAX_REQUEST_OVERHEAD + 1;
    char *buf = (char *)http_alloc(cap);
    if (!buf)
        return NULL;
    int len = snprintf(buf, cap,
                       "%s %s HTTP/1.1\r\n"
                       "Host: %s\r\n"
                       "Connection: close\r\n"
                       "%s"
                       "Content-Length: %zu\r\n"
                       "\r\n",
                       method, path, host, headers ? headers : "", body_len);
    if (len < 0 || (size_t)len >= cap)
    {
        http_dealloc(buf);
        return NULL;
    }
    *out_len = (size_t)len;
    return buf;
}

/* Parse a raw HTTP response buffer into resp.  buf[len] must be '\0'. */
HTTP_STATIC int parse_http_response(char *buf, size_t len, http_response_t *resp)
{
    char *header_end = strstr(buf, "\r\n\r\n");
    if (!header_end)
        return -1;

    resp->status_code = 0;
    if (strncmp(buf, "HTTP/", 5) == 0)
    {
        const char *sp = strchr(buf, ' ');
        if (sp)
            resp->status_code = atoi(sp + 1);
    }

    size_t hdr_len = (size_t)(header_end - buf);
    resp->headers = (char *)http_alloc(hdr_len + 1);
    if (!resp->headers)
        return -1;
    memcpy(resp->headers, buf, hdr_len);
    resp->headers[hdr_len] = '\0';

    char *body_start = header_end + 4;
    resp->body_len = len - (size_t)(body_start - buf);
    resp->body = (char *)http_alloc(resp->body_len + 1);
    if (!resp->body)
    {
        http_dealloc(resp->headers);
        resp->headers = NULL;
        return -1;
    }
    memcpy(resp->body, body_start, resp->body_len);
    resp->body[resp->body_len] = '\0';
    return 0;
}

/* Ensure buf has room for len+1 bytes, failing if len >= max_bytes.
 * Doubles the buffer on demand.  Returns NULL (buf freed) on error. */
static char *ensure_resp_capacity(char *buf, size_t *cap, size_t len)
{
    if (len >= HTTP_MAX_RESPONSE_BODY_BYTES)
    {
        http_dealloc(buf);
        return NULL;
    }
    if (len + 1 < *cap)
        return buf;
    char *nb = (char *)http_realloc(buf, *cap * 2);
    if (!nb)
    {
        http_dealloc(buf);
        return NULL;
    }
    *cap *= 2;
    return nb;
}

/* Read from sock until the peer closes.  Returns a NUL-terminated malloc'd
 * buffer on success (caller frees); sets *out_len to byte count.  NULL on
 * error (oversized response, OOM, or connect failure). */
static char *recv_tcp_response(vigil_socket_t sock, size_t *out_len)
{
    size_t cap = 8192, len = 0;
    char *buf = (char *)http_alloc(cap);
    if (!buf)
        return NULL;

    for (;;)
    {
        size_t n = 0;
        vigil_status_t st = vigil_platform_tcp_recv(sock, buf + len, cap - len - 1, &n, NULL);
        if (st != VIGIL_STATUS_OK || n == 0)
            break;
        len += n;
        buf = ensure_resp_capacity(buf, &cap, len);
        if (!buf)
            return NULL;
    }
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

HTTP_STATIC int socket_request(const char *method, parsed_url_t *url, const char *headers, const char *body,
                               size_t body_len, http_response_t *resp)
{
    memset(resp, 0, sizeof(*resp));
    vigil_platform_net_init(NULL);

    vigil_socket_t sock = VIGIL_INVALID_SOCKET;
    if (vigil_platform_tcp_connect(url->host, url->port, &sock, NULL) != VIGIL_STATUS_OK)
        return -1;

    vigil_platform_tcp_set_timeout(sock, HTTP_DEFAULT_TIMEOUT_MS, NULL);

    size_t req_len = 0;
    char *req_buf = alloc_request_buf(method, url->path, url->host, headers, body_len, &req_len);
    if (!req_buf)
    {
        vigil_platform_tcp_close(sock, NULL);
        return -1;
    }

    if (tcp_send_all(sock, req_buf, req_len) != 0)
    {
        http_dealloc(req_buf);
        vigil_platform_tcp_close(sock, NULL);
        return -1;
    }
    http_dealloc(req_buf);

    if (body && body_len > 0 && tcp_send_all(sock, body, body_len) != 0)
    {
        vigil_platform_tcp_close(sock, NULL);
        return -1;
    }

    size_t resp_len = 0;
    char *buf = recv_tcp_response(sock, &resp_len);
    vigil_platform_tcp_close(sock, NULL);
    if (!buf)
        return -1;

    int result = parse_http_response(buf, resp_len, resp);
    http_dealloc(buf);
    return result;
}

/* ── BearSSL TLS fallback client ─────────────────────────────────── */

#ifdef VIGIL_ENABLE_BEARSSL_TLS
#include "bearssl.h"

/* ---------- Socket I/O callbacks for BearSSL ---------------------- */

static int tls_read_cb(void *ctx, unsigned char *buf, size_t len)
{
    vigil_socket_t sock = *(vigil_socket_t *)ctx;
    size_t n = 0;
    vigil_status_t st = vigil_platform_tcp_recv(sock, buf, len, &n, NULL);
    return (st != VIGIL_STATUS_OK || n == 0) ? -1 : (int)n;
}

static int tls_write_cb(void *ctx, const unsigned char *buf, size_t len)
{
    vigil_socket_t sock = *(vigil_socket_t *)ctx;
    size_t n = 0;
    return vigil_platform_tcp_send(sock, buf, len, &n, NULL) != VIGIL_STATUS_OK ? -1 : (int)n;
}

/* ---------- Insecure x509 validator (test / no-CA-bundle) --------- */
/* Accepts any server certificate without verification.  Only used when
 * bearssl_https_insecure_request() is called directly (unit tests). */

typedef struct
{
    const br_x509_class *vtable; /* must be first */
    br_x509_decoder_context xc;
    int in_ee; /* 1 while decoding the end-entity cert */
} br_x509_insecure_context;

static void x509_ins_dn_cb(void *ctx, const void *buf, size_t len)
{
    (void)ctx;
    (void)buf;
    (void)len;
}

static void x509_ins_start_chain(const br_x509_class **ctx, const char *server_name)
{
    br_x509_insecure_context *xc = (br_x509_insecure_context *)(void *)ctx;
    br_x509_decoder_init(&xc->xc, x509_ins_dn_cb, NULL);
    xc->in_ee = 1;
    (void)server_name;
}

static void x509_ins_start_cert(const br_x509_class **ctx, uint32_t length)
{
    (void)ctx;
    (void)length;
}

static void x509_ins_append(const br_x509_class **ctx, const unsigned char *buf, size_t len)
{
    br_x509_insecure_context *xc = (br_x509_insecure_context *)(void *)ctx;
    if (xc->in_ee)
        br_x509_decoder_push(&xc->xc, buf, len);
}

static void x509_ins_end_cert(const br_x509_class **ctx)
{
    br_x509_insecure_context *xc = (br_x509_insecure_context *)(void *)ctx;
    xc->in_ee = 0; /* only decode the first (EE) cert */
}

static unsigned x509_ins_end_chain(const br_x509_class **ctx)
{
    (void)ctx;
    return 0; /* accept unconditionally */
}

static const br_x509_pkey *x509_ins_get_pkey(const br_x509_class *const *ctx, unsigned *usages)
{
    const br_x509_insecure_context *xc = (const br_x509_insecure_context *)(const void *)ctx;
    if (usages)
        *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
    /* br_x509_decoder_get_pkey takes a non-const pointer; cast is safe
     * because we own the context stored inside this const view. */
    return br_x509_decoder_get_pkey((br_x509_decoder_context *)(void *)&xc->xc);
}

static const br_x509_class tls_x509_insecure_vtable = {
    sizeof(br_x509_insecure_context),
    x509_ins_start_chain,
    x509_ins_start_cert,
    x509_ins_append,
    x509_ins_end_cert,
    x509_ins_end_chain,
    x509_ins_get_pkey,
};

/* ---------- Platform CA bundle loading ---------------------------- */

#define TLS_MAX_TAS 512

typedef struct
{
    unsigned char dn[512];
    size_t dn_len;
} tls_dn_acc_t;

static void tls_dn_cb(void *ctx, const void *buf, size_t len)
{
    tls_dn_acc_t *acc = (tls_dn_acc_t *)ctx;
    if (acc->dn_len + len < sizeof(acc->dn))
    {
        memcpy(acc->dn + acc->dn_len, buf, len);
        acc->dn_len += len;
    }
}

static int tls_copy_ec_key(br_x509_pkey *dst, br_x509_pkey *src)
{
    dst->key_type = BR_KEYTYPE_EC;
    dst->key.ec.curve = src->key.ec.curve;
    dst->key.ec.q = (unsigned char *)http_alloc(src->key.ec.qlen);
    if (!dst->key.ec.q)
        return 0;
    memcpy(dst->key.ec.q, src->key.ec.q, src->key.ec.qlen);
    dst->key.ec.qlen = src->key.ec.qlen;
    return 1;
}

static int tls_copy_rsa_key(br_x509_pkey *dst, br_x509_pkey *src)
{
    dst->key_type = BR_KEYTYPE_RSA;
    dst->key.rsa.n = (unsigned char *)http_alloc(src->key.rsa.nlen);
    dst->key.rsa.e = (unsigned char *)http_alloc(src->key.rsa.elen);
    if (!dst->key.rsa.n || !dst->key.rsa.e)
    {
        http_dealloc(dst->key.rsa.n);
        http_dealloc(dst->key.rsa.e);
        return 0;
    }
    memcpy(dst->key.rsa.n, src->key.rsa.n, src->key.rsa.nlen);
    dst->key.rsa.nlen = src->key.rsa.nlen;
    memcpy(dst->key.rsa.e, src->key.rsa.e, src->key.rsa.elen);
    dst->key.rsa.elen = src->key.rsa.elen;
    return 1;
}

/* Decode one DER certificate and append a trust anchor to *tas[*count]. */
static int tls_decode_ta(const unsigned char *der, size_t der_len, br_x509_trust_anchor *ta)
{
    tls_dn_acc_t dn_acc;
    br_x509_decoder_context xc;

    memset(&dn_acc, 0, sizeof(dn_acc));
    br_x509_decoder_init(&xc, tls_dn_cb, &dn_acc);
    br_x509_decoder_push(&xc, der, der_len);
    if (br_x509_decoder_last_error(&xc) != 0)
        return 0;

    br_x509_pkey *pk = br_x509_decoder_get_pkey(&xc);
    if (!pk || dn_acc.dn_len == 0)
        return 0;

    ta->dn.data = (unsigned char *)http_alloc(dn_acc.dn_len + 1);
    if (!ta->dn.data)
        return 0;
    memcpy(ta->dn.data, dn_acc.dn, dn_acc.dn_len);
    ta->dn.len = dn_acc.dn_len;
    ta->flags = BR_X509_TA_CA;

    if (pk->key_type == BR_KEYTYPE_EC)
        return tls_copy_ec_key(&ta->pkey, pk);
    if (pk->key_type == BR_KEYTYPE_RSA)
        return tls_copy_rsa_key(&ta->pkey, pk);
    http_dealloc(ta->dn.data);
    ta->dn.data = NULL;
    return 0;
}

/* Load trust anchors via the platform abstraction layer.
 * vigil_platform_enumerate_tls_cas is implemented in src/platform/ and
 * handles Windows (WinCrypt), macOS (Security.framework), and Linux/BSD
 * (PEM file probing) without pulling platform headers into this layer. */

typedef struct
{
    br_x509_trust_anchor *tas;
    size_t max_tas;
    size_t count;
} tls_ca_ctx_t;

static int tls_ca_enum_cb(const unsigned char *der, size_t len, void *userdata)
{
    tls_ca_ctx_t *ctx = (tls_ca_ctx_t *)userdata;
    if (ctx->count < ctx->max_tas && tls_decode_ta(der, len, &ctx->tas[ctx->count]))
        ctx->count++;
    return 0;
}

static int tls_load_platform_cas(br_x509_trust_anchor *tas, size_t max_tas)
{
    tls_ca_ctx_t ctx = {tas, max_tas, 0};
    if (vigil_platform_enumerate_tls_cas(tls_ca_enum_cb, &ctx) < 0)
        return -1;
    return (int)ctx.count;
}

static void tls_free_tas(br_x509_trust_anchor *tas, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++)
    {
        http_dealloc(tas[i].dn.data);
        if (tas[i].pkey.key_type == BR_KEYTYPE_EC)
            http_dealloc(tas[i].pkey.key.ec.q);
        else if (tas[i].pkey.key_type == BR_KEYTYPE_RSA)
        {
            http_dealloc(tas[i].pkey.key.rsa.n);
            http_dealloc(tas[i].pkey.key.rsa.e);
        }
    }
}

/* ---------- TLS request helpers ----------------------------------- */

/* Bundles the five HTTP request parameters so helpers stay under the param limit. */
typedef struct
{
    const char *method;
    const parsed_url_t *url;
    const char *headers;
    const char *body;
    size_t body_len;
} tls_req_t;

static int tls_send_request(br_sslio_context *ioc, const tls_req_t *req)
{
    size_t req_len = 0;
    char *req_buf =
        alloc_request_buf(req->method, req->url->path, req->url->host, req->headers, req->body_len, &req_len);
    if (!req_buf)
        return -1;
    int ok = (br_sslio_write_all(ioc, req_buf, req_len) == 0);
    http_dealloc(req_buf);
    if (!ok)
        return -1;
    if (req->body_len > 0 && req->body != NULL)
    {
        if (br_sslio_write_all(ioc, req->body, req->body_len) != 0)
            return -1;
    }
    return br_sslio_flush(ioc) == 0 ? 0 : -1;
}

static char *tls_recv_response(br_sslio_context *ioc, size_t *out_len)
{
    size_t cap = 8192, len = 0;
    char *buf = (char *)http_alloc(cap);
    if (!buf)
        return NULL;

    for (;;)
    {
        int n = br_sslio_read(ioc, buf + len, cap - len - 1);
        if (n < 0)
            break; /* EOF or error — handled by caller */
        len += (size_t)n;
        buf = ensure_resp_capacity(buf, &cap, len);
        if (!buf)
            return NULL;
    }
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

/* Core TLS request: sc must already be initialised with the right x509 engine. */
static int bearssl_do_https(const tls_req_t *req, http_response_t *resp, br_ssl_client_context *sc)
{
    vigil_socket_t sock = VIGIL_INVALID_SOCKET;
    if (vigil_platform_tcp_connect(req->url->host, req->url->port, &sock, NULL) != VIGIL_STATUS_OK)
        return -1;
    vigil_platform_tcp_set_timeout(sock, HTTP_DEFAULT_TIMEOUT_MS, NULL);

    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    br_ssl_engine_set_buffer(&sc->eng, iobuf, sizeof(iobuf), 1);
    br_ssl_client_reset(sc, req->url->host, 0);

    br_sslio_context ioc;
    br_sslio_init(&ioc, &sc->eng, tls_read_cb, &sock, tls_write_cb, &sock);

    if (tls_send_request(&ioc, req) != 0)
    {
        vigil_platform_tcp_close(sock, NULL);
        return -1;
    }

    size_t resp_len = 0;
    char *resp_buf = tls_recv_response(&ioc, &resp_len);
    vigil_platform_tcp_close(sock, NULL);
    if (!resp_buf)
        return -1;

    memset(resp, 0, sizeof(*resp));
    int result = parse_http_response(resp_buf, resp_len, resp);
    http_dealloc(resp_buf);
    return result;
}

/* Public: HTTPS with platform CA verification. */
HTTP_STATIC int bearssl_https_request(const char *method, const parsed_url_t *url, const char *headers,
                                      const char *body, size_t body_len, http_response_t *resp)
{
    br_x509_trust_anchor *tas = (br_x509_trust_anchor *)http_alloc(TLS_MAX_TAS * sizeof(br_x509_trust_anchor));
    if (!tas)
        return -1;

    int ta_count = tls_load_platform_cas(tas, TLS_MAX_TAS);
    if (ta_count <= 0)
    {
        http_dealloc(tas);
        fprintf(stderr, "vigil: HTTPS: no system CA bundle found\n");
        return -1;
    }

    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    br_ssl_client_init_full(&sc, &xc, tas, (size_t)ta_count);

    tls_req_t req = {method, url, headers, body, body_len};
    int result = bearssl_do_https(&req, resp, &sc);
    tls_free_tas(tas, (size_t)ta_count);
    http_dealloc(tas);
    return result;
}

/* Public: HTTPS skipping certificate verification (test / emergency use). */
HTTP_STATIC int bearssl_https_insecure_request(const char *method, const parsed_url_t *url, const char *headers,
                                               const char *body, size_t body_len, http_response_t *resp)
{
    br_x509_insecure_context ins_xc;
    ins_xc.vtable = &tls_x509_insecure_vtable;
    ins_xc.in_ee = 0;

    br_ssl_client_context sc;
    br_x509_minimal_context xc_dummy;
    br_ssl_client_init_full(&sc, &xc_dummy, NULL, 0);
    br_ssl_engine_set_x509(&sc.eng, &ins_xc.vtable);

    tls_req_t req = {method, url, headers, body, body_len};
    return bearssl_do_https(&req, resp, &sc);
}

#endif /* VIGIL_ENABLE_BEARSSL_TLS */

/* ── Constants ───────────────────────────────────────────────────── */

#define HTTP_MAX_REDIRECTS 10

/* ── Unified client request ──────────────────────────────────────── */

/* ── Cookie jar helpers ──────────────────────────────────────────── */

HTTP_STATIC void cookie_jar_append(char **jar, const char *val, size_t vlen)
{
    while (vlen > 0 && (unsigned char)val[vlen - 1] <= ' ')
        vlen--;
    if (vlen == 0)
        return;
    size_t old = *jar ? strlen(*jar) : 0;
    size_t sep = (old > 0) ? 2U : 0U;
    char *n = (char *)http_realloc(*jar, old + sep + vlen + 1);
    if (!n)
        return;
    if (sep)
    {
        n[old] = ';';
        n[old + 1] = ' ';
    }
    memcpy(n + old + sep, val, vlen);
    n[old + sep + vlen] = '\0';
    *jar = n;
}

HTTP_STATIC int hdr_name_matches(const char *line, const char *name, size_t namelen)
{
    size_t i;
    for (i = 0; i < namelen; i++)
    {
        char c = line[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (c != name[i])
            return 0;
    }
    return line[namelen] == ':';
}

HTTP_STATIC void collect_cookies(const char *hdrs, char **jar)
{
    const char *p = hdrs;
    if (!hdrs)
        return;
    while (*p)
    {
        const char *eol = strstr(p, "\r\n");
        if (!eol)
            eol = p + strlen(p);
        if ((size_t)(eol - p) > 11 && hdr_name_matches(p, "set-cookie", 10))
        {
            const char *val = p + 11;
            while (val < eol && *val == ' ')
                val++;
            const char *semi = (const char *)memchr(val, ';', (size_t)(eol - val));
            size_t vlen = semi ? (size_t)(semi - val) : (size_t)(eol - val);
            cookie_jar_append(jar, val, vlen);
        }
        if (*eol == '\0')
            break;
        p = eol + 2;
    }
}

HTTP_STATIC char *build_request_headers(const char *cookie_jar, const char *existing)
{
    size_t clen, elen, off;
    char *out;

    if (!cookie_jar || !*cookie_jar)
        return NULL;
    clen = strlen(cookie_jar);
    elen = existing ? strlen(existing) : 0;
    out = (char *)http_alloc(9 + clen + 2 + elen + 1);
    if (!out)
        return NULL;
    off = 0;
    memcpy(out + off, "Cookie: ", 8);
    off += 8;
    memcpy(out + off, cookie_jar, clen);
    off += clen;
    out[off++] = '\r';
    out[off++] = '\n';
    if (existing && elen > 0)
    {
        memcpy(out + off, existing, elen);
        off += elen;
    }
    out[off] = '\0';
    return out;
}

HTTP_STATIC int do_request_once(const char *method, const char *url_str, const char *headers, const char *body,
                                size_t body_len, http_response_t *resp)
{
    memset(resp, 0, sizeof(*resp));

    /* Try native HTTP library first (supports HTTPS). */
    vigil_http_response_t native_resp;
    vigil_status_t st = vigil_platform_http_request(method, url_str, headers, body, body_len, &native_resp, NULL);

    if (st == VIGIL_STATUS_OK)
    {
        resp->status_code = native_resp.status_code;
        resp->headers = native_resp.headers;
        resp->body = native_resp.body;
        resp->body_len = native_resp.body_len;
        return 0;
    }

    /* Native lib unavailable — fall back to sockets (HTTP only). */
    parsed_url_t url;
    if (!parse_url(url_str, &url))
        return -1;

    if (strcmp(url.scheme, "https") == 0)
    {
#ifdef VIGIL_ENABLE_BEARSSL_TLS
        return bearssl_https_request(method, &url, headers, body, body_len, resp);
#else
        fprintf(stderr, "vigil: HTTPS requires libcurl (not available); request aborted\n");
        return -1;
#endif
    }
    return socket_request(method, &url, headers, body, body_len, resp);
}

/* Return 1 if the header line [p, eol) looks like a Location: field. */
static int is_location_line(const char *p, const char *eol)
{
    if (eol - p <= 10)
        return 0;
    if ((p[0] != 'L' && p[0] != 'l') || (p[1] != 'o' && p[1] != 'O'))
        return 0;
    return p[8] == ':' || p[9] == ':';
}

/* If [p, eol) is a Location header, return pointer past leading spaces in
 * the value; otherwise return NULL. */
static const char *extract_location_value(const char *p, const char *eol)
{
    if (!is_location_line(p, eol))
        return NULL;
    const char *colon = strchr(p, ':');
    if (!colon || colon >= eol)
        return NULL;
    const char *val = colon + 1;
    while (val < eol && *val == ' ')
        val++;
    return val;
}

/* If line [p, eol) is a Location header whose value fits in buf, copy it and
 * return buf; otherwise return NULL. */
static const char *try_copy_location(const char *p, const char *eol, char *buf, size_t buf_sz)
{
    const char *val = extract_location_value(p, eol);
    if (!val)
        return NULL;
    size_t vlen = (size_t)(eol - val);
    if (vlen >= buf_sz)
        return NULL;
    memcpy(buf, val, vlen);
    buf[vlen] = '\0';
    return buf;
}

/* Extract the Location header value from a response headers string.
 * Returns buf on success (NUL-terminated), NULL if not found or too long. */
HTTP_STATIC const char *find_location_header(const char *hdrs, char *buf, size_t buf_sz)
{
    if (!hdrs)
        return NULL;
    for (const char *p = hdrs; *p;)
    {
        const char *eol = strstr(p, "\r\n");
        if (!eol)
            eol = p + strlen(p);
        const char *found = try_copy_location(p, eol, buf, buf_sz);
        if (found)
            return found;
        if (*eol == '\0')
            break;
        p = eol + 2;
    }
    return NULL;
}

HTTP_STATIC int redirect_changes_method(int code)
{
    return code == 301 || code == 302 || code == 303;
}

/* Return 1 if code is a redirect that should be followed (excludes 304/305). */
static int is_redirect_status(int code)
{
    return code >= 301 && code <= 308 && code != 304 && code != 305;
}

/* If resp is a followable redirect with a valid Location, update url_buf and
 * (for 301/302/303) reset method to GET.  Frees resp and returns 1 on
 * success; returns 0 without modifying anything on failure. */
static int apply_redirect(http_response_t *resp, char *url_buf, size_t url_buf_sz, const char **method_out,
                          const char **body_out, size_t *body_len_out)
{
    if (!is_redirect_status(resp->status_code))
        return 0;
    char loc[4096];
    if (!find_location_header(resp->headers, loc, sizeof(loc)))
        return 0;
    int code = resp->status_code;
    response_free(resp);
    size_t ulen = strlen(loc);
    if (ulen >= url_buf_sz)
        return 0;
    memcpy(url_buf, loc, ulen + 1);
    if (redirect_changes_method(code))
    {
        *method_out = "GET";
        *body_out = NULL;
        *body_len_out = 0;
    }
    return 1;
}

HTTP_STATIC int do_request(const char *method, const char *url_str, const char *headers, const char *body,
                           size_t body_len, http_response_t *resp)
{
    char url_buf[4096];
    char *cookie_jar = NULL;
    size_t ulen = strlen(url_str);
    if (ulen >= sizeof(url_buf))
        ulen = sizeof(url_buf) - 1;
    memcpy(url_buf, url_str, ulen);
    url_buf[ulen] = '\0';

    int result = 0;
    for (int redirects = 0; redirects <= HTTP_MAX_REDIRECTS; redirects++)
    {
        char *combined = build_request_headers(cookie_jar, headers);
        result = do_request_once(method, url_buf, combined ? combined : headers, body, body_len, resp);
        http_dealloc(combined);
        if (result != 0)
            break;
        collect_cookies(resp->headers, &cookie_jar);
        if (!apply_redirect(resp, url_buf, sizeof(url_buf), &method, &body, &body_len))
            break;
    }
    http_dealloc(cookie_jar);
    return result;
}

/* ── HTTP server via platform TCP sockets ────────────────────────── */

#define HTTP_MAX_SERVERS 64
#define HTTP_MAX_CLIENTS 256
#define HTTP_MAX_ROUTES 128

typedef struct
{
    char *pattern;
    vigil_object_t *handler; /* zero-arity function */
} http_route_t;

typedef struct
{
    vigil_socket_t listener;
    int in_use;
    http_route_t routes[HTTP_MAX_ROUTES];
    int route_count;
    int read_timeout_ms;
    int write_timeout_ms;
    int idle_timeout_ms;
} http_server_t;

typedef struct
{
    vigil_socket_t sock;
    int in_use;
    char *method;
    char *path;
    char *headers;
    char *body;
    size_t body_len;
    char *pending_cookies;
    size_t cookies_len;
} http_conn_t;

static http_server_t g_servers[HTTP_MAX_SERVERS];
static http_conn_t g_clients[HTTP_MAX_CLIENTS];
static int g_inited = 0;

static void http_tables_init(void)
{
    if (g_inited)
        return;
    vigil_platform_net_init(NULL);
    for (int i = 0; i < HTTP_MAX_SERVERS; i++)
    {
        g_servers[i].listener = VIGIL_INVALID_SOCKET;
        g_servers[i].in_use = 0;
    }
    for (int i = 0; i < HTTP_MAX_CLIENTS; i++)
    {
        g_clients[i].sock = VIGIL_INVALID_SOCKET;
        g_clients[i].in_use = 0;
    }
    g_inited = 1;
}

static int64_t alloc_server(vigil_socket_t s)
{
    for (int i = 0; i < HTTP_MAX_SERVERS; i++)
    {
        if (!g_servers[i].in_use)
        {
            g_servers[i].listener = s;
            g_servers[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

static void client_free_fields(http_conn_t *c)
{
    http_dealloc(c->method);
    c->method = NULL;
    http_dealloc(c->path);
    c->path = NULL;
    http_dealloc(c->headers);
    c->headers = NULL;
    http_dealloc(c->body);
    c->body = NULL;
    c->body_len = 0;
    http_dealloc(c->pending_cookies);
    c->pending_cookies = NULL;
    c->cookies_len = 0;
}

static int64_t alloc_client(vigil_socket_t s)
{
    for (int i = 0; i < HTTP_MAX_CLIENTS; i++)
    {
        if (!g_clients[i].in_use)
        {
            memset(&g_clients[i], 0, sizeof(g_clients[i]));
            g_clients[i].sock = s;
            g_clients[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

static http_conn_t *get_client(int64_t h)
{
    if (h < 0 || h >= HTTP_MAX_CLIENTS || !g_clients[h].in_use)
        return NULL;
    return &g_clients[h];
}

/* Parse an incoming HTTP request from a connected socket. */
/* Ensure header buffer has room for len+1 bytes, growing up to
 * HTTP_MAX_HEADER_BYTES.  Returns NULL (buf freed) on limit or OOM. */
HTTP_STATIC char *ensure_hdr_capacity(char *buf, size_t *cap, size_t len)
{
    if (len + 1 < *cap)
        return buf;
    if (*cap >= HTTP_MAX_HEADER_BYTES)
    {
        http_dealloc(buf);
        return NULL;
    }
    size_t new_cap = *cap * 2;
    if (new_cap > HTTP_MAX_HEADER_BYTES)
        new_cap = HTTP_MAX_HEADER_BYTES;
    char *nb = (char *)http_realloc(buf, new_cap);
    if (!nb)
    {
        http_dealloc(buf);
        return NULL;
    }
    *cap = new_cap;
    return nb;
}

/* Read from sock until "\r\n\r\n" is seen.  Returns a NUL-terminated
 * malloc'd buffer (caller frees), or NULL on error or oversized headers. */
HTTP_STATIC char *recv_request_headers(vigil_socket_t sock, size_t *out_len)
{
    size_t cap = 8192, len = 0;
    char *buf = (char *)http_alloc(cap);
    if (!buf)
        return NULL;

    for (;;)
    {
        size_t n = 0;
        vigil_status_t st = vigil_platform_tcp_recv(sock, buf + len, cap - len - 1, &n, NULL);
        if (st != VIGIL_STATUS_OK || n == 0)
        {
            http_dealloc(buf);
            return NULL;
        }
        len += n;
        buf[len] = '\0';
        if (strstr(buf, "\r\n\r\n"))
            break;
        buf = ensure_hdr_capacity(buf, &cap, len);
        if (!buf)
            return NULL;
    }
    *out_len = len;
    return buf;
}

/* Parse "METHOD PATH HTTP/x.x\r\n" at buf[0..line_end).
 * Sets *method_out and *path_out to newly malloc'd strings.
 * Returns 0 on success; both pointers are NULL on failure. */
HTTP_STATIC int parse_request_line(const char *buf, const char *line_end, char **method_out, char **path_out)
{
    const char *sp1 = strchr(buf, ' ');
    if (!sp1 || sp1 > line_end)
        return -1;
    char *method = (char *)http_alloc((size_t)(sp1 - buf) + 1);
    if (!method)
        return -1;
    memcpy(method, buf, (size_t)(sp1 - buf));
    method[sp1 - buf] = '\0';

    const char *path_start = sp1 + 1;
    const char *sp2 = strchr(path_start, ' ');
    if (!sp2 || sp2 > line_end)
    {
        http_dealloc(method);
        return -1;
    }
    char *path = (char *)http_alloc((size_t)(sp2 - path_start) + 1);
    if (!path)
    {
        http_dealloc(method);
        return -1;
    }
    memcpy(path, path_start, (size_t)(sp2 - path_start));
    path[sp2 - path_start] = '\0';

    *method_out = method;
    *path_out = path;
    return 0;
}

/* Extract Content-Length from headers.  Returns -1 if the value exceeds
 * HTTP_MAX_BODY_BYTES, 1 if found and valid (sets *out), 0 if absent. */
HTTP_STATIC int parse_content_length(const char *headers, size_t *out)
{
    *out = 0;
    const char *cl = strstr(headers, "Content-Length:");
    if (!cl)
        cl = strstr(headers, "content-length:");
    if (!cl)
        return 0;
    unsigned long long v = strtoull(cl + 15, NULL, 10);
    if (v > HTTP_MAX_BODY_BYTES)
        return -1;
    *out = (size_t)v;
    return 1;
}

/* Read exactly content_length bytes into body (pre-allocated, room for +1).
 * already bytes are pre-filled from bstart.  Returns 0 on success. */
HTTP_STATIC int recv_body_bytes(vigil_socket_t sock, char *body, const char *bstart, size_t already,
                                size_t content_length)
{
    if (already > 0)
        memcpy(body, bstart, already);
    size_t got = already;
    while (got < content_length)
    {
        size_t n = 0;
        if (vigil_platform_tcp_recv(sock, body + got, content_length - got, &n, NULL) != VIGIL_STATUS_OK || n == 0)
            return -1;
        got += n;
    }
    body[content_length] = '\0';
    return 0;
}

/* Build the request body from bytes already in the header buffer plus any
 * remaining bytes read from sock.  Returns a malloc'd NUL-terminated buffer
 * (caller frees) and sets *body_len_out; NULL on error. */
HTTP_STATIC char *recv_request_body(vigil_socket_t sock, const char *headers, const char *bstart, size_t already,
                                    size_t *body_len_out)
{
    if (already > HTTP_MAX_BODY_BYTES)
        return NULL;
    size_t content_length;
    if (parse_content_length(headers, &content_length) < 0)
        return NULL;

    if (content_length > already)
    {
        char *body = (char *)http_alloc(content_length + 1);
        if (!body)
            return NULL;
        if (recv_body_bytes(sock, body, bstart, already, content_length) != 0)
        {
            http_dealloc(body);
            return NULL;
        }
        *body_len_out = content_length;
        return body;
    }

    /* No Content-Length or body already fully buffered. */
    char *body = (char *)http_alloc(already + 1);
    if (!body)
        return NULL;
    if (already > 0)
        memcpy(body, bstart, already);
    body[already] = '\0';
    *body_len_out = already;
    return body;
}

HTTP_STATIC int parse_incoming_request(http_conn_t *conn)
{
    size_t hdr_buf_len = 0;
    char *buf = recv_request_headers(conn->sock, &hdr_buf_len);
    if (!buf)
        return -1;

    char *line_end = strstr(buf, "\r\n");
    if (!line_end)
    {
        http_dealloc(buf);
        return -1;
    }

    char *method = NULL, *path = NULL;
    if (parse_request_line(buf, line_end, &method, &path) != 0)
    {
        http_dealloc(buf);
        return -1;
    }

    char *hdr_start = line_end + 2;
    char *hdr_end = strstr(buf, "\r\n\r\n");
    size_t hdr_len = (size_t)(hdr_end - hdr_start);
    char *headers = (char *)http_alloc(hdr_len + 1);
    if (!headers)
    {
        http_dealloc(method);
        http_dealloc(path);
        http_dealloc(buf);
        return -1;
    }
    memcpy(headers, hdr_start, hdr_len);
    headers[hdr_len] = '\0';

    char *bstart = hdr_end + 4;
    size_t already = hdr_buf_len - (size_t)(bstart - buf);
    size_t body_len = 0;
    char *body = recv_request_body(conn->sock, headers, bstart, already, &body_len);
    http_dealloc(buf);
    if (!body)
    {
        http_dealloc(method);
        http_dealloc(path);
        http_dealloc(headers);
        return -1;
    }

    conn->method = method;
    conn->path = path;
    conn->headers = headers;
    conn->body = body;
    conn->body_len = body_len;
    return 0;
}

/* ── Server VIGIL functions ───────────────────────────────────────── */

static vigil_status_t http_listen(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    g_http_alloc = get_alloc(vm);
    g_http_alloc_set = 1;
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *host = NULL;
    size_t host_len = 0;
    if (!get_string_arg(vm, base, 0, &host, &host_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    int64_t port = get_i64_arg(vm, base, 1);
    char host_buf[256];
    if (host_len >= sizeof(host_buf))
        host_len = sizeof(host_buf) - 1;
    memcpy(host_buf, host, host_len);
    host_buf[host_len] = '\0';
    vigil_vm_stack_pop_n(vm, arg_count);

    http_tables_init();
    vigil_socket_t sock = VIGIL_INVALID_SOCKET;
    if (vigil_platform_tcp_listen(host_buf, (int)port, &sock, error) != VIGIL_STATUS_OK)
        return push_i64(vm, -1, error);

    int64_t handle = alloc_server(sock);
    if (handle < 0)
    {
        vigil_platform_tcp_close(sock, NULL);
    }
    return push_i64(vm, handle, error);
}

static vigil_status_t http_accept(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    g_http_alloc = get_alloc(vm);
    g_http_alloc_set = 1;
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t srv = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (srv < 0 || srv >= HTTP_MAX_SERVERS || !g_servers[srv].in_use)
        return push_i64(vm, -1, error);

    vigil_socket_t client = VIGIL_INVALID_SOCKET;
    if (vigil_platform_tcp_accept(g_servers[srv].listener, &client, NULL) != VIGIL_STATUS_OK)
        return push_i64(vm, -1, error);

    int64_t ch = alloc_client(client);
    if (ch < 0)
    {
        vigil_platform_tcp_close(client, NULL);
        return push_i64(vm, -1, error);
    }

    http_conn_t *conn = get_client(ch);
    if (parse_incoming_request(conn) != 0)
    {
        client_free_fields(conn);
        vigil_platform_tcp_close(conn->sock, NULL);
        conn->sock = VIGIL_INVALID_SOCKET;
        conn->in_use = 0;
        return push_i64(vm, -1, error);
    }
    return push_i64(vm, ch, error);
}

static vigil_status_t http_req_method(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    http_conn_t *c = get_client(ch);
    const char *m = (c && c->method) ? c->method : "";
    return push_string(vm, m, strlen(m), error);
}

static vigil_status_t http_req_path(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    http_conn_t *c = get_client(ch);
    const char *p = (c && c->path) ? c->path : "";
    return push_string(vm, p, strlen(p), error);
}

static vigil_status_t http_req_body(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    http_conn_t *c = get_client(ch);
    const char *b = (c && c->body) ? c->body : "";
    return push_string(vm, b, c ? c->body_len : 0, error);
}

static vigil_status_t http_req_headers(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    http_conn_t *c = get_client(ch);
    const char *h = (c && c->headers) ? c->headers : "";
    return push_string(vm, h, strlen(h), error);
}

static size_t copy_name_to_buf(const char *name, size_t name_len, char *buf, size_t buf_size)
{
    if (name == NULL || name_len == 0)
    {
        buf[0] = '\0';
        return 0;
    }
    if (name_len >= buf_size)
        name_len = buf_size - 1;
    memcpy(buf, name, name_len);
    buf[name_len] = '\0';
    return name_len;
}

static vigil_status_t http_req_header(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    const char *name = NULL;
    size_t name_len = 0;
    get_string_arg(vm, base, 1, &name, &name_len);
    char nbuf[256];
    name_len = copy_name_to_buf(name, name_len, nbuf, sizeof(nbuf));
    vigil_vm_stack_pop_n(vm, arg_count);

    http_conn_t *c = get_client(ch);
    if (!c || !c->headers)
        return push_string(vm, "", 0, error);

    /* Case-insensitive search for "Name:" in headers */
    const char *p = c->headers;
    while (*p)
    {
        const char *eol = strstr(p, "\r\n");
        if (!eol)
            eol = p + strlen(p);
        const char *colon = strchr(p, ':');
        if (colon && colon < eol)
        {
            size_t klen = (size_t)(colon - p);
            if (klen == name_len)
            {
                int match = 1;
                for (size_t i = 0; i < klen; i++)
                {
                    char a = p[i], b = nbuf[i];
                    if (a >= 'A' && a <= 'Z')
                        a += 32;
                    if (b >= 'A' && b <= 'Z')
                        b += 32;
                    if (a != b)
                    {
                        match = 0;
                        break;
                    }
                }
                if (match)
                {
                    const char *val = colon + 1;
                    while (val < eol && *val == ' ')
                        val++;
                    return push_string(vm, val, (size_t)(eol - val), error);
                }
            }
        }
        if (*eol == '\0')
            break;
        p = eol + 2;
    }
    return push_string(vm, "", 0, error);
}

static vigil_status_t http_req_query(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    http_conn_t *c = get_client(ch);
    if (!c || !c->path)
        return push_string(vm, "", 0, error);
    const char *q = strchr(c->path, '?');
    if (q)
        return push_string(vm, q + 1, strlen(q + 1), error);
    return push_string(vm, "", 0, error);
}

static const char *http_reason_phrase(int code)
{
    switch (code)
    {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 204:
        return "No Content";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 304:
        return "Not Modified";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 500:
        return "Internal Server Error";
    case 502:
        return "Bad Gateway";
    case 503:
        return "Service Unavailable";
    default:
        return "OK";
    }
}

static vigil_status_t http_respond(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    g_http_alloc = get_alloc(vm);
    g_http_alloc_set = 1;
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    int64_t status_code = get_i64_arg(vm, base, 1);
    const char *hdrs = NULL, *body = NULL;
    size_t hdrs_len = 0, body_len = 0;
    get_string_arg(vm, base, 2, &hdrs, &hdrs_len);
    get_string_arg(vm, base, 3, &body, &body_len);

    char *hc = NULL, *bc = NULL;
    if (hdrs_len > 0)
    {
        hc = (char *)http_alloc(hdrs_len + 1);
        memcpy(hc, hdrs, hdrs_len);
        hc[hdrs_len] = '\0';
    }
    if (body_len > 0)
    {
        bc = (char *)http_alloc(body_len + 1);
        memcpy(bc, body, body_len);
        bc[body_len] = '\0';
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    http_conn_t *c = get_client(ch);
    if (!c || c->sock == VIGIL_INVALID_SOCKET)
    {
        http_dealloc(hc);
        http_dealloc(bc);
        return push_i64(vm, -1, error);
    }

    const char *reason = http_reason_phrase((int)status_code);

    char resp_hdr[4096];
    int hlen =
        snprintf(resp_hdr, sizeof(resp_hdr), "HTTP/1.1 %d %s\r\n%s%sContent-Length: %zu\r\nConnection: close\r\n\r\n",
                 (int)status_code, reason, hc ? hc : "", c->pending_cookies ? c->pending_cookies : "", body_len);

    int rc = 0;
    if (vigil_platform_tcp_send(c->sock, resp_hdr, (size_t)hlen, NULL, NULL) != VIGIL_STATUS_OK)
        rc = -1;
    if (rc == 0 && bc && body_len > 0)
    {
        if (vigil_platform_tcp_send(c->sock, bc, body_len, NULL, NULL) != VIGIL_STATUS_OK)
            rc = -1;
    }
    http_dealloc(hc);
    http_dealloc(bc);

    vigil_platform_tcp_close(c->sock, NULL);
    c->sock = VIGIL_INVALID_SOCKET;
    client_free_fields(c);
    c->in_use = 0;
    return push_i64(vm, rc, error);
}

static void server_free_routes(http_server_t *srv)
{
    for (int i = 0; i < srv->route_count; i++)
    {
        http_dealloc(srv->routes[i].pattern);
        if (srv->routes[i].handler)
            vigil_object_release(&srv->routes[i].handler);
    }
    srv->route_count = 0;
}

static vigil_status_t http_server_close(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    g_http_alloc = get_alloc(vm);
    g_http_alloc_set = 1;
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (h >= 0 && h < HTTP_MAX_SERVERS && g_servers[h].in_use)
    {
        vigil_platform_tcp_close(g_servers[h].listener, NULL);
        server_free_routes(&g_servers[h]);
        g_servers[h].listener = VIGIL_INVALID_SOCKET;
        g_servers[h].in_use = 0;
    }
    vigil_value_t nil = vigil_nanbox_encode_int(0);
    return vigil_vm_stack_push(vm, &nil, error);
}

static vigil_status_t http_set_read_timeout(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = get_i64_arg(vm, base, 0);
    int64_t ms = get_i64_arg(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (h >= 0 && h < HTTP_MAX_SERVERS && g_servers[h].in_use)
        g_servers[h].read_timeout_ms = (int)ms;
    return push_i64(vm, 0, error);
}

static vigil_status_t http_set_write_timeout(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = get_i64_arg(vm, base, 0);
    int64_t ms = get_i64_arg(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (h >= 0 && h < HTTP_MAX_SERVERS && g_servers[h].in_use)
        g_servers[h].write_timeout_ms = (int)ms;
    return push_i64(vm, 0, error);
}

static vigil_status_t http_set_idle_timeout(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t h = get_i64_arg(vm, base, 0);
    int64_t ms = get_i64_arg(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (h >= 0 && h < HTTP_MAX_SERVERS && g_servers[h].in_use)
        g_servers[h].idle_timeout_ms = (int)ms;
    return push_i64(vm, 0, error);
}

/* ── Routing and serve loop ──────────────────────────────────────── */

/* Per-thread current connection — use platform thread-local if available,
   otherwise a simple global (safe for single-threaded serve). */
static volatile int64_t g_current_conn = -1;

typedef struct
{
    vigil_runtime_t *runtime;
    vigil_object_t *handler;
    int64_t conn_handle;
    int read_timeout_ms;
    int write_timeout_ms;
} serve_thread_ctx_t;

static void serve_thread_entry(void *arg)
{
    serve_thread_ctx_t *ctx = (serve_thread_ctx_t *)arg;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};

    g_current_conn = ctx->conn_handle;

    /* Apply timeouts to the connection socket */
    http_conn_t *c = get_client(ctx->conn_handle);
    if (c && c->sock != VIGIL_INVALID_SOCKET)
    {
        int timeout = ctx->read_timeout_ms > ctx->write_timeout_ms ? ctx->read_timeout_ms : ctx->write_timeout_ms;
        if (timeout > 0)
            vigil_platform_tcp_set_timeout(c->sock, timeout, NULL);
    }

    if (vigil_vm_open(&vm, ctx->runtime, NULL, &error) == VIGIL_STATUS_OK)
    {
        vigil_value_t out = {0};
        vigil_vm_execute_function(vm, ctx->handler, &out, &error);
        vigil_vm_close(&vm);
    }

    /* Clean up if handler didn't call respond */
    http_conn_t *conn = get_client(ctx->conn_handle);
    if (conn && conn->sock != VIGIL_INVALID_SOCKET)
    {
        vigil_platform_tcp_close(conn->sock, NULL);
        conn->sock = VIGIL_INVALID_SOCKET;
    }
    if (conn)
    {
        client_free_fields(conn);
        conn->in_use = 0;
    }

    vigil_object_release(&ctx->handler);
    http_dealloc(ctx);
}

static vigil_status_t http_handle(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    g_http_alloc = get_alloc(vm);
    g_http_alloc_set = 1;
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t srv_h = get_i64_arg(vm, base, 0);
    const char *pattern = NULL;
    size_t plen = 0;
    get_string_arg(vm, base, 1, &pattern, &plen);
    vigil_value_t fn_val = vigil_vm_stack_get(vm, base + 2);
    vigil_object_t *fn = vigil_value_as_object(&fn_val);

    char *pcopy = NULL;
    if (plen > 0)
    {
        pcopy = (char *)http_alloc(plen + 1);
        memcpy(pcopy, pattern, plen);
        pcopy[plen] = '\0';
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (srv_h < 0 || srv_h >= HTTP_MAX_SERVERS || !g_servers[srv_h].in_use || !fn ||
        g_servers[srv_h].route_count >= HTTP_MAX_ROUTES)
    {
        http_dealloc(pcopy);
        return push_i64(vm, -1, error);
    }

    vigil_object_retain(fn);
    http_route_t *r = &g_servers[srv_h].routes[g_servers[srv_h].route_count++];
    r->pattern = pcopy;
    r->handler = fn;
    return push_i64(vm, 0, error);
}

HTTP_STATIC int route_matches(const char *pattern, const char *path)
{
    if (!pattern || !path)
        return 0;
    size_t plen = strlen(pattern);
    /* Exact match */
    if (strcmp(pattern, path) == 0)
        return 1;
    /* Prefix match: pattern ends with '/' and path starts with pattern */
    if (plen > 0 && pattern[plen - 1] == '/' && strncmp(path, pattern, plen) == 0)
        return 1;
    /* Path without query matches pattern */
    const char *q = strchr(path, '?');
    if (q)
    {
        size_t path_len = (size_t)(q - path);
        if (path_len == plen && strncmp(path, pattern, plen) == 0)
            return 1;
    }
    return 0;
}

static vigil_status_t http_serve(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    g_http_alloc = get_alloc(vm);
    g_http_alloc_set = 1;
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t srv_h = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (srv_h < 0 || srv_h >= HTTP_MAX_SERVERS || !g_servers[srv_h].in_use)
        return push_i64(vm, -1, error);

    http_server_t *srv = &g_servers[srv_h];
    vigil_runtime_t *runtime = vigil_vm_runtime(vm);

    for (;;)
    {
        vigil_socket_t client = VIGIL_INVALID_SOCKET;
        if (vigil_platform_tcp_accept(srv->listener, &client, NULL) != VIGIL_STATUS_OK)
            break;

        int64_t ch = alloc_client(client);
        if (ch < 0)
        {
            vigil_platform_tcp_close(client, NULL);
            continue;
        }

        http_conn_t *conn = get_client(ch);
        if (parse_incoming_request(conn) != 0)
        {
            client_free_fields(conn);
            vigil_platform_tcp_close(conn->sock, NULL);
            conn->sock = VIGIL_INVALID_SOCKET;
            conn->in_use = 0;
            continue;
        }

        /* Find matching route */
        vigil_object_t *handler = NULL;
        for (int i = 0; i < srv->route_count; i++)
        {
            if (route_matches(srv->routes[i].pattern, conn->path))
            {
                handler = srv->routes[i].handler;
                break;
            }
        }

        if (handler)
        {
            serve_thread_ctx_t *ctx = (serve_thread_ctx_t *)http_alloc(sizeof(*ctx));
            ctx->runtime = runtime;
            ctx->handler = handler;
            ctx->conn_handle = ch;
            ctx->read_timeout_ms = srv->read_timeout_ms;
            ctx->write_timeout_ms = srv->write_timeout_ms;
            vigil_object_retain(handler);

            vigil_platform_thread_t *t = NULL;
            if (vigil_platform_thread_create(&t, serve_thread_entry, ctx, NULL) == VIGIL_STATUS_OK)
            {
                /* Detach — thread cleans up after itself */
                (void)t;
            }
            else
            {
                vigil_object_release(&ctx->handler);
                http_dealloc(ctx);
                vigil_platform_tcp_close(conn->sock, NULL);
                conn->sock = VIGIL_INVALID_SOCKET;
                client_free_fields(conn);
                conn->in_use = 0;
            }
        }
        else
        {
            /* 404 for unmatched routes */
            const char *body_404 = "404 Not Found";
            char hdr[256];
            int hlen =
                snprintf(hdr, sizeof(hdr), "HTTP/1.1 404 Not Found\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                         strlen(body_404));
            vigil_platform_tcp_send(conn->sock, hdr, (size_t)hlen, NULL, NULL);
            vigil_platform_tcp_send(conn->sock, body_404, strlen(body_404), NULL, NULL);
            vigil_platform_tcp_close(conn->sock, NULL);
            conn->sock = VIGIL_INVALID_SOCKET;
            client_free_fields(conn);
            conn->in_use = 0;
        }
    }

    return push_i64(vm, 0, error);
}

static vigil_status_t http_current_conn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, g_current_conn, error);
}

/* ── Streaming response ──────────────────────────────────────────── */

static vigil_status_t http_write_header(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    g_http_alloc = get_alloc(vm);
    g_http_alloc_set = 1;
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    int64_t status_code = get_i64_arg(vm, base, 1);
    const char *hdrs = NULL;
    size_t hdrs_len = 0;
    if (arg_count >= 3)
        get_string_arg(vm, base, 2, &hdrs, &hdrs_len);

    char *hc = NULL;
    if (hdrs_len > 0)
    {
        hc = (char *)http_alloc(hdrs_len + 1);
        memcpy(hc, hdrs, hdrs_len);
        hc[hdrs_len] = '\0';
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    http_conn_t *c = get_client(ch);
    if (!c || c->sock == VIGIL_INVALID_SOCKET)
    {
        http_dealloc(hc);
        return push_i64(vm, -1, error);
    }

    const char *reason = "OK";
    switch ((int)status_code)
    {
    case 200:
        reason = "OK";
        break;
    case 201:
        reason = "Created";
        break;
    case 204:
        reason = "No Content";
        break;
    case 400:
        reason = "Bad Request";
        break;
    case 404:
        reason = "Not Found";
        break;
    case 500:
        reason = "Internal Server Error";
        break;
    default:
        break;
    }

    char resp_hdr[4096];
    int hlen = snprintf(resp_hdr, sizeof(resp_hdr),
                        "HTTP/1.1 %d %s\r\n%s%sTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n",
                        (int)status_code, reason, hc ? hc : "", c->pending_cookies ? c->pending_cookies : "");
    http_dealloc(hc);

    int rc = 0;
    if (vigil_platform_tcp_send(c->sock, resp_hdr, (size_t)hlen, NULL, NULL) != VIGIL_STATUS_OK)
        rc = -1;
    return push_i64(vm, rc, error);
}

static vigil_status_t http_write(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    g_http_alloc = get_alloc(vm);
    g_http_alloc_set = 1;
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    const char *data = NULL;
    size_t dlen = 0;
    get_string_arg(vm, base, 1, &data, &dlen);
    char *dc = NULL;
    if (dlen > 0)
    {
        dc = (char *)http_alloc(dlen);
        memcpy(dc, data, dlen);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    http_conn_t *c = get_client(ch);
    if (!c || c->sock == VIGIL_INVALID_SOCKET || dlen == 0)
    {
        http_dealloc(dc);
        return push_i64(vm, dlen == 0 ? 0 : -1, error);
    }

    /* Send as HTTP chunked encoding */
    char chunk_hdr[32];
    int chlen = snprintf(chunk_hdr, sizeof(chunk_hdr), "%zx\r\n", dlen);
    int rc = 0;
    if (vigil_platform_tcp_send(c->sock, chunk_hdr, (size_t)chlen, NULL, NULL) != VIGIL_STATUS_OK)
        rc = -1;
    if (rc == 0 && vigil_platform_tcp_send(c->sock, dc, dlen, NULL, NULL) != VIGIL_STATUS_OK)
        rc = -1;
    if (rc == 0 && vigil_platform_tcp_send(c->sock, "\r\n", 2, NULL, NULL) != VIGIL_STATUS_OK)
        rc = -1;
    http_dealloc(dc);
    return push_i64(vm, rc, error);
}

static vigil_status_t http_flush(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    g_http_alloc = get_alloc(vm);
    g_http_alloc_set = 1;
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    http_conn_t *c = get_client(ch);
    if (!c || c->sock == VIGIL_INVALID_SOCKET)
        return push_i64(vm, -1, error);

    /* Send final chunk (zero-length) to end chunked transfer */
    int rc = 0;
    if (vigil_platform_tcp_send(c->sock, "0\r\n\r\n", 5, NULL, NULL) != VIGIL_STATUS_OK)
        rc = -1;
    vigil_platform_tcp_close(c->sock, NULL);
    c->sock = VIGIL_INVALID_SOCKET;
    client_free_fields(c);
    c->in_use = 0;
    return push_i64(vm, rc, error);
}

static vigil_status_t http_redirect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    g_http_alloc = get_alloc(vm);
    g_http_alloc_set = 1;
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    const char *url = NULL;
    size_t url_len = 0;
    get_string_arg(vm, base, 1, &url, &url_len);
    int64_t status_code = arg_count >= 3 ? get_i64_arg(vm, base, 2) : 302;

    char ubuf[2048];
    url_len = copy_name_to_buf(url, url_len, ubuf, sizeof(ubuf));
    vigil_vm_stack_pop_n(vm, arg_count);

    http_conn_t *c = get_client(ch);
    if (!c || c->sock == VIGIL_INVALID_SOCKET)
        return push_i64(vm, -1, error);

    const char *reason = "Found";
    if (status_code == 301)
        reason = "Moved Permanently";
    else if (status_code == 307)
        reason = "Temporary Redirect";
    else if (status_code == 308)
        reason = "Permanent Redirect";

    char hdr[4096];
    int hlen =
        snprintf(hdr, sizeof(hdr), "HTTP/1.1 %d %s\r\nLocation: %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                 (int)status_code, reason, ubuf);
    vigil_platform_tcp_send(c->sock, hdr, (size_t)hlen, NULL, NULL);
    vigil_platform_tcp_close(c->sock, NULL);
    c->sock = VIGIL_INVALID_SOCKET;
    client_free_fields(c);
    c->in_use = 0;
    return push_i64(vm, 0, error);
}

/* ── Client: redirect following ──────────────────────────────────── */

/* ── Cookie helpers ──────────────────────────────────────────────── */

static vigil_status_t http_set_cookie(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    g_http_alloc = get_alloc(vm);
    g_http_alloc_set = 1;
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    const char *name = NULL, *value = NULL;
    size_t nlen = 0, vlen = 0;
    get_string_arg(vm, base, 1, &name, &nlen);
    get_string_arg(vm, base, 2, &value, &vlen);
    const char *opts = NULL;
    size_t olen = 0;
    if (arg_count >= 4)
        get_string_arg(vm, base, 3, &opts, &olen);

    char cookie[4096];
    int clen;
    if (opts && olen > 0)
        clen = snprintf(cookie, sizeof(cookie), "Set-Cookie: %.*s=%.*s; %.*s\r\n", (int)nlen, name, (int)vlen, value,
                        (int)olen, opts);
    else
        clen = snprintf(cookie, sizeof(cookie), "Set-Cookie: %.*s=%.*s\r\n", (int)nlen, name, (int)vlen, value);
    vigil_vm_stack_pop_n(vm, arg_count);

    http_conn_t *c = get_client(ch);
    if (!c)
        return push_i64(vm, -1, error);

    /* Append to pending_cookies buffer */
    size_t new_len = c->cookies_len + (size_t)clen;
    char *nb = (char *)http_realloc(c->pending_cookies, new_len + 1);
    if (!nb)
        return push_i64(vm, -1, error);
    memcpy(nb + c->cookies_len, cookie, (size_t)clen);
    nb[new_len] = '\0';
    c->pending_cookies = nb;
    c->cookies_len = new_len;
    return push_i64(vm, 0, error);
}

static vigil_status_t http_req_cookies(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    g_http_alloc = get_alloc(vm);
    g_http_alloc_set = 1;
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ch = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    http_conn_t *c = get_client(ch);
    if (!c || !c->headers)
        return push_string(vm, "", 0, error);

    /* Find Cookie: header */
    const char *p = c->headers;
    while (*p)
    {
        const char *eol = strstr(p, "\r\n");
        if (!eol)
            eol = p + strlen(p);
        if ((eol - p > 7) && (p[0] == 'C' || p[0] == 'c') && (p[1] == 'o' || p[1] == 'O') && (p[6] == ':'))
        {
            const char *val = p + 7;
            while (val < eol && *val == ' ')
                val++;
            return push_string(vm, val, (size_t)(eol - val), error);
        }
        if (*eol == '\0')
            break;
        p = eol + 2;
    }
    return push_string(vm, "", 0, error);
}

/* ── Client VIGIL functions ───────────────────────────────────────── */

static vigil_status_t http_get(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    g_http_alloc = get_alloc(vm);
    g_http_alloc_set = 1;
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *url = NULL;
    size_t url_len = 0;
    if (!get_string_arg(vm, base, 0, &url, &url_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        push_string(vm, "", 0, error);
        push_string(vm, "", 0, error);
        return push_i64(vm, -1, error);
    }
    char *uc = (char *)http_alloc(url_len + 1);
    memcpy(uc, url, url_len);
    uc[url_len] = '\0';
    vigil_vm_stack_pop_n(vm, arg_count);

    http_response_t resp;
    int rc = do_request("GET", uc, NULL, NULL, 0, &resp);
    http_dealloc(uc);
    if (rc != 0)
    {
        push_string(vm, "", 0, error);
        push_string(vm, "", 0, error);
        return push_i64(vm, -1, error);
    }
    int status = resp.status_code;
    push_string(vm, resp.headers ? resp.headers : "", resp.headers ? strlen(resp.headers) : 0, error);
    vigil_status_t st = push_string(vm, resp.body ? resp.body : "", resp.body_len, error);
    response_free(&resp);
    return st != VIGIL_STATUS_OK ? st : push_i64(vm, status, error);
}

static vigil_status_t http_post(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    g_http_alloc = get_alloc(vm);
    g_http_alloc_set = 1;
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *url, *body, *ct = NULL;
    size_t url_len, body_len, ct_len = 0;
    if (!get_string_arg(vm, base, 0, &url, &url_len) || !get_string_arg(vm, base, 1, &body, &body_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        push_string(vm, "", 0, error);
        push_string(vm, "", 0, error);
        return push_i64(vm, -1, error);
    }
    if (arg_count >= 3)
        get_string_arg(vm, base, 2, &ct, &ct_len);
    char *uc = (char *)http_alloc(url_len + 1);
    memcpy(uc, url, url_len);
    uc[url_len] = '\0';
    char *bc = (char *)http_alloc(body_len + 1);
    memcpy(bc, body, body_len);
    bc[body_len] = '\0';
    char hdrs[512] = "";
    if (ct && ct_len > 0)
        snprintf(hdrs, sizeof(hdrs), "Content-Type: %.*s\r\n", (int)ct_len, ct);
    vigil_vm_stack_pop_n(vm, arg_count);

    http_response_t resp;
    int rc = do_request("POST", uc, hdrs, bc, body_len, &resp);
    http_dealloc(uc);
    http_dealloc(bc);
    if (rc != 0)
    {
        push_string(vm, "", 0, error);
        push_string(vm, "", 0, error);
        return push_i64(vm, -1, error);
    }
    int status = resp.status_code;
    push_string(vm, resp.headers ? resp.headers : "", resp.headers ? strlen(resp.headers) : 0, error);
    vigil_status_t st = push_string(vm, resp.body ? resp.body : "", resp.body_len, error);
    response_free(&resp);
    return st != VIGIL_STATUS_OK ? st : push_i64(vm, status, error);
}

static vigil_status_t http_request(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    g_http_alloc = get_alloc(vm);
    g_http_alloc_set = 1;
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *method, *url, *headers = NULL, *body = NULL;
    size_t ml, ul, hl = 0, bl = 0;
    if (!get_string_arg(vm, base, 0, &method, &ml) || !get_string_arg(vm, base, 1, &url, &ul))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        push_string(vm, "", 0, error);
        push_string(vm, "", 0, error);
        return push_i64(vm, -1, error);
    }
    if (arg_count >= 3)
        get_string_arg(vm, base, 2, &headers, &hl);
    if (arg_count >= 4)
        get_string_arg(vm, base, 3, &body, &bl);
    char *mc = (char *)http_alloc(ml + 1);
    memcpy(mc, method, ml);
    mc[ml] = '\0';
    char *uc = (char *)http_alloc(ul + 1);
    memcpy(uc, url, ul);
    uc[ul] = '\0';
    char *hc = hl ? (char *)http_alloc(hl + 1) : NULL;
    char *bc = bl ? (char *)http_alloc(bl + 1) : NULL;
    if (hc)
    {
        memcpy(hc, headers, hl);
        hc[hl] = '\0';
    }
    if (bc)
    {
        memcpy(bc, body, bl);
        bc[bl] = '\0';
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    http_response_t resp;
    int rc = do_request(mc, uc, hc, bc, bl, &resp);
    http_dealloc(mc);
    http_dealloc(uc);
    http_dealloc(hc);
    http_dealloc(bc);
    if (rc != 0)
    {
        push_string(vm, "", 0, error);
        push_string(vm, "", 0, error);
        return push_i64(vm, -1, error);
    }
    int status = resp.status_code;
    push_string(vm, resp.headers ? resp.headers : "", resp.headers ? strlen(resp.headers) : 0, error);
    vigil_status_t st = push_string(vm, resp.body ? resp.body : "", resp.body_len, error);
    response_free(&resp);
    return st != VIGIL_STATUS_OK ? st : push_i64(vm, status, error);
}

/* ── Module descriptor ───────────────────────────────────────────── */

static const int http_1str[] = {VIGIL_TYPE_STRING};
static const int http_2str[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int http_str_i32[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_I32};
static const int http_i64[] = {VIGIL_TYPE_I64};
static const int http_respond_p[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};

static const int http_i64_str[] = {VIGIL_TYPE_I64, VIGIL_TYPE_STRING};
static const int http_i64_str_str[] = {VIGIL_TYPE_I64, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int http_i64_i64[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64};
static const int http_handle_p[] = {VIGIL_TYPE_I64, VIGIL_TYPE_STRING, VIGIL_TYPE_OBJECT};
static const int http_ret_get[] = {VIGIL_TYPE_I32, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};

static const vigil_native_module_function_t http_functions[] = {
    {"get", 3, http_get, 1, http_1str, VIGIL_TYPE_I32, 3, http_ret_get, 0, NULL, NULL},
    {"post", 4, http_post, 2, http_2str, VIGIL_TYPE_I32, 3, http_ret_get, 0, NULL, NULL},
    {"request", 7, http_request, 2, http_2str, VIGIL_TYPE_I32, 3, http_ret_get, 0, NULL, NULL},
    {"listen", 6, http_listen, 2, http_str_i32, VIGIL_TYPE_I64, 1, NULL, 0, NULL, NULL},
    {"accept", 6, http_accept, 1, http_i64, VIGIL_TYPE_I64, 1, NULL, 0, NULL, NULL},
    {"handle", 6, http_handle, 3, http_handle_p, VIGIL_TYPE_I32, 1, NULL, 0, NULL, NULL},
    {"serve", 5, http_serve, 1, http_i64, VIGIL_TYPE_I32, 1, NULL, 0, NULL, NULL},
    {"current_conn", 12, http_current_conn, 0, NULL, VIGIL_TYPE_I64, 1, NULL, 0, NULL, NULL},
    {"write_header", 12, http_write_header, 2, http_i64_i64, VIGIL_TYPE_I32, 1, NULL, 0, NULL, NULL},
    {"write", 5, http_write, 2, http_i64_str, VIGIL_TYPE_I32, 1, NULL, 0, NULL, NULL},
    {"flush", 5, http_flush, 1, http_i64, VIGIL_TYPE_I32, 1, NULL, 0, NULL, NULL},
    {"req_method", 10, http_req_method, 1, http_i64, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL},
    {"req_path", 8, http_req_path, 1, http_i64, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL},
    {"req_body", 8, http_req_body, 1, http_i64, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL},
    {"req_headers", 11, http_req_headers, 1, http_i64, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL},
    {"req_header", 10, http_req_header, 2, http_i64_str, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL},
    {"req_query", 9, http_req_query, 1, http_i64, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL},
    {"respond", 7, http_respond, 4, http_respond_p, VIGIL_TYPE_I32, 1, NULL, 0, NULL, NULL},
    {"redirect", 8, http_redirect, 2, http_i64_str, VIGIL_TYPE_I32, 1, NULL, 0, NULL, NULL},
    {"set_cookie", 10, http_set_cookie, 3, http_i64_str_str, VIGIL_TYPE_I32, 1, NULL, 0, NULL, NULL},
    {"req_cookies", 11, http_req_cookies, 1, http_i64, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL},
    {"close", 5, http_server_close, 1, http_i64, VIGIL_TYPE_VOID, 0, NULL, 0, NULL, NULL},
    {"set_read_timeout", 16, http_set_read_timeout, 2, http_i64_i64, VIGIL_TYPE_I32, 1, NULL, 0, NULL, NULL},
    {"set_write_timeout", 17, http_set_write_timeout, 2, http_i64_i64, VIGIL_TYPE_I32, 1, NULL, 0, NULL, NULL},
    {"set_idle_timeout", 16, http_set_idle_timeout, 2, http_i64_i64, VIGIL_TYPE_I32, 1, NULL, 0, NULL, NULL},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_http = {
    "http", 4, http_functions, sizeof(http_functions) / sizeof(http_functions[0]), NULL, 0};
