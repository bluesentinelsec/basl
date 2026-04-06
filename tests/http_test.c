/* Unit tests for VIGIL http module. */
#include "vigil_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"

/* ── Declarations for http.c internals (VIGIL_HTTP_TESTING) ───────── */

typedef struct
{
    char scheme[16];
    char host[256];
    int port;
    char path[2048];
} parsed_url_t;

typedef struct
{
    int status_code;
    char *headers;
    char *body;
    size_t body_len;
} http_response_t;

extern int parse_url(const char *url, parsed_url_t *out);
extern void response_free(http_response_t *r);
extern int parse_http_response(char *buf, size_t len, http_response_t *resp);
extern int socket_request(const char *method, parsed_url_t *url, const char *headers, const char *body, size_t body_len,
                          http_response_t *resp);
extern int do_request(const char *method, const char *url_str, const char *headers, const char *body, size_t body_len,
                      http_response_t *resp);
extern void cookie_jar_append(char **jar, const char *val, size_t vlen);
extern int hdr_name_matches(const char *line, const char *name, size_t namelen);
extern void collect_cookies(const char *hdrs, char **jar);
extern char *build_request_headers(const char *cookie_jar, const char *existing);
extern int redirect_changes_method(int code);
extern const char *find_location_header(const char *hdrs, char *buf, size_t buf_sz);
extern int route_matches(const char *pattern, const char *path);

/* Server-side request parsing helpers (exposed via HTTP_STATIC) */
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

extern char *ensure_hdr_capacity(char *buf, size_t *cap, size_t len);
extern int parse_request_line(const char *buf, const char *line_end, char **method_out, char **path_out);
extern int parse_content_length(const char *headers, size_t *out);
extern int recv_body_bytes(vigil_socket_t sock, char *body, const char *bstart, size_t already, size_t content_length);
extern char *recv_request_body(vigil_socket_t sock, const char *headers, const char *bstart, size_t already,
                               size_t *body_len_out);
extern char *recv_request_headers(vigil_socket_t sock, size_t *out_len);
extern int parse_incoming_request(http_conn_t *conn);

#ifdef VIGIL_ENABLE_BEARSSL_TLS
extern int bearssl_https_insecure_request(const char *method, const parsed_url_t *url, const char *headers,
                                          const char *body, size_t body_len, http_response_t *resp);
#endif

/* ── URL parsing tests (table-style) ─────────────────────────────── */

TEST(VigilHttpTest, ParseUrlFull)
{
    parsed_url_t u;
    ASSERT_TRUE(parse_url("https://example.com:8080/api/v1", &u));
    EXPECT_STREQ(u.scheme, "https");
    EXPECT_STREQ(u.host, "example.com");
    EXPECT_EQ(u.port, 8080);
    EXPECT_TRUE(strcmp(u.path, "/api/v1") == 0);
}

TEST(VigilHttpTest, ParseUrlHttpDefault)
{
    parsed_url_t u;
    ASSERT_TRUE(parse_url("http://localhost/test", &u));
    EXPECT_STREQ(u.scheme, "http");
    EXPECT_STREQ(u.host, "localhost");
    EXPECT_EQ(u.port, 80);
    EXPECT_TRUE(strcmp(u.path, "/test") == 0);
}

TEST(VigilHttpTest, ParseUrlHttpsDefaultPort)
{
    parsed_url_t u;
    ASSERT_TRUE(parse_url("https://example.com/secure", &u));
    EXPECT_EQ(u.port, 443);
}

TEST(VigilHttpTest, ParseUrlNoPath)
{
    parsed_url_t u;
    ASSERT_TRUE(parse_url("http://example.com", &u));
    EXPECT_STREQ(u.host, "example.com");
    EXPECT_TRUE(strcmp(u.path, "/") == 0);
}

TEST(VigilHttpTest, ParseUrlNoScheme)
{
    parsed_url_t u;
    ASSERT_TRUE(parse_url("example.com/foo", &u));
    EXPECT_STREQ(u.scheme, "http");
    EXPECT_STREQ(u.host, "example.com");
    EXPECT_TRUE(strcmp(u.path, "/foo") == 0);
}

TEST(VigilHttpTest, ParseUrlLoopback)
{
    parsed_url_t u;
    ASSERT_TRUE(parse_url("http://127.0.0.1:9100/data", &u));
    EXPECT_STREQ(u.host, "127.0.0.1");
    EXPECT_EQ(u.port, 9100);
    EXPECT_TRUE(strcmp(u.path, "/data") == 0);
}

/* ── Loopback test server ────────────────────────────────────────── */

#define TEST_PORT 18787

typedef struct
{
    vigil_socket_t listener;
    volatile int ready;
    const char *response; /* full HTTP response to send */
    int port;             /* 0 = use TEST_PORT */
} test_server_t;

static void test_server_func(void *arg)
{
    test_server_t *srv = (test_server_t *)arg;
    int listen_port = srv->port > 0 ? srv->port : TEST_PORT;

    if (vigil_platform_tcp_listen("127.0.0.1", listen_port, &srv->listener, NULL) != VIGIL_STATUS_OK)
    {
        srv->listener = VIGIL_INVALID_SOCKET;
        return;
    }

    srv->ready = 1;

    vigil_socket_t client = VIGIL_INVALID_SOCKET;
    if (vigil_platform_tcp_accept(srv->listener, &client, NULL) != VIGIL_STATUS_OK)
        return;

    /* Drain the request */
    char buf[4096];
    size_t n = 0;
    vigil_platform_tcp_recv(client, buf, sizeof(buf), &n, NULL);

    /* Send response */
    const char *resp = srv->response;
    vigil_platform_tcp_send(client, resp, strlen(resp), NULL, NULL);
    vigil_platform_tcp_close(client, NULL);
}

static int start_test_server_port(test_server_t *srv, const char *response, int port, vigil_platform_thread_t **thread)
{
    memset(srv, 0, sizeof(*srv));
    srv->listener = VIGIL_INVALID_SOCKET;
    srv->response = response;
    srv->port = port;

    vigil_platform_net_init(NULL);
    vigil_status_t st = vigil_platform_thread_create(thread, test_server_func, srv, NULL);
    if (st != VIGIL_STATUS_OK)
        return 0;

    for (int i = 0; i < 200 && !srv->ready; i++)
        vigil_platform_thread_sleep(10);
    return srv->ready;
}

static int start_test_server(test_server_t *srv, const char *response, vigil_platform_thread_t **thread)
{
    return start_test_server_port(srv, response, TEST_PORT, thread);
}

static void stop_test_server(test_server_t *srv, vigil_platform_thread_t *thread)
{
    if (srv->listener != VIGIL_INVALID_SOCKET)
        vigil_platform_tcp_close(srv->listener, NULL);
    vigil_platform_thread_join(thread, NULL);
}

/* ── Socket client tests against loopback server ─────────────────── */

TEST(VigilHttpTest, SocketGetBasic)
{
    test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    const char *canned = "HTTP/1.1 200 OK\r\n"
                         "Content-Length: 5\r\n"
                         "\r\n"
                         "hello";

    if (!start_test_server(&srv, canned, &thr))
    {
        /* Port busy — skip gracefully */
        return;
    }

    parsed_url_t url;
    parse_url("http://127.0.0.1:18787/test", &url);

    http_response_t resp;
    int rc = socket_request("GET", &url, NULL, NULL, 0, &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 200);
    EXPECT_STREQ(resp.body, "hello");
    EXPECT_EQ(resp.body_len, 5);
    response_free(&resp);

    stop_test_server(&srv, thr);
}

TEST(VigilHttpTest, SocketPostBody)
{
    test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    const char *canned = "HTTP/1.1 201 Created\r\n"
                         "Content-Length: 2\r\n"
                         "\r\n"
                         "ok";

    if (!start_test_server(&srv, canned, &thr))
        return;

    parsed_url_t url;
    parse_url("http://127.0.0.1:18787/submit", &url);

    http_response_t resp;
    const char *body = "{\"key\":\"val\"}";
    int rc = socket_request("POST", &url, "Content-Type: application/json\r\n", body, strlen(body), &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 201);
    EXPECT_STREQ(resp.body, "ok");
    response_free(&resp);

    stop_test_server(&srv, thr);
}

TEST(VigilHttpTest, Socket404Response)
{
    test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    const char *canned = "HTTP/1.1 404 Not Found\r\n"
                         "Content-Length: 9\r\n"
                         "\r\n"
                         "not found";

    if (!start_test_server(&srv, canned, &thr))
        return;

    parsed_url_t url;
    parse_url("http://127.0.0.1:18787/missing", &url);

    http_response_t resp;
    int rc = socket_request("GET", &url, NULL, NULL, 0, &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 404);
    EXPECT_STREQ(resp.body, "not found");
    response_free(&resp);

    stop_test_server(&srv, thr);
}

TEST(VigilHttpTest, SocketEmptyBody)
{
    test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    const char *canned = "HTTP/1.1 204 No Content\r\n"
                         "Content-Length: 0\r\n"
                         "\r\n";

    if (!start_test_server(&srv, canned, &thr))
        return;

    parsed_url_t url;
    parse_url("http://127.0.0.1:18787/empty", &url);

    http_response_t resp;
    int rc = socket_request("DELETE", &url, NULL, NULL, 0, &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 204);
    EXPECT_EQ(resp.body_len, 0);
    response_free(&resp);

    stop_test_server(&srv, thr);
}

TEST(VigilHttpTest, SocketConnectionRefused)
{
    parsed_url_t url;
    parse_url("http://127.0.0.1:18788/nothing", &url);

    http_response_t resp;
    int rc = socket_request("GET", &url, NULL, NULL, 0, &resp);
    EXPECT_EQ(rc, -1);
}

TEST(VigilHttpTest, SocketRequestLargeHeadersSucceed)
{
    /* Dynamic request buffer accommodates large custom headers. */
    test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    const char *canned = "HTTP/1.1 200 OK\r\n"
                         "Content-Length: 2\r\n"
                         "\r\n"
                         "ok";
    char *headers = NULL;

    if (!start_test_server(&srv, canned, &thr))
        return;

    headers = (char *)malloc(5001);
    ASSERT_TRUE(headers != NULL);
    memset(headers, 'a', 5000);
    headers[0] = 'X';
    headers[1] = ':';
    headers[4998] = '\r';
    headers[4999] = '\n';
    headers[5000] = '\0';

    parsed_url_t url;
    parse_url("http://127.0.0.1:18787/large-headers", &url);

    http_response_t resp;
    int rc = socket_request("GET", &url, headers, NULL, 0, &resp);
    EXPECT_EQ(rc, 0);
    if (rc == 0)
        response_free(&resp);

    free(headers);
    stop_test_server(&srv, thr);
}

TEST(VigilHttpTest, ParseUrlUppercaseScheme)
{
    parsed_url_t u;
    EXPECT_TRUE(parse_url("HTTPS://example.com/path", &u));
    EXPECT_STREQ(u.scheme, "https");
    EXPECT_EQ(u.port, 443);
}

TEST(VigilHttpTest, ParseUrlPathTooLong)
{
    /* Path longer than 2047 chars must return 0 (error). */
    char url[4096];
    size_t i;
    memcpy(url, "http://example.com/", 19);
    for (i = 19; i < sizeof(url) - 1; i++)
        url[i] = 'a';
    url[sizeof(url) - 1] = '\0';

    parsed_url_t u;
    EXPECT_EQ(parse_url(url, &u), 0);
}

/* ── Cookie jar / helper unit tests ──────────────────────────────── */

TEST(VigilHttpTest, CookieJarAppend)
{
    char *jar = NULL;
    cookie_jar_append(&jar, "a=1", 3);
    EXPECT_TRUE(jar != NULL);
    EXPECT_STREQ(jar, "a=1");
    /* Second append adds "; " separator; trailing spaces trimmed */
    cookie_jar_append(&jar, "b=2  ", 5);
    EXPECT_TRUE(strstr(jar, "a=1; b=2") != NULL);
    free(jar);
}

TEST(VigilHttpTest, CookieJarAppendNoOp)
{
    /* Zero-length value leaves jar unchanged */
    char *jar = NULL;
    cookie_jar_append(&jar, "", 0);
    EXPECT_EQ(jar, NULL);
    cookie_jar_append(&jar, "a=1", 3);
    char *prev = jar;
    cookie_jar_append(&jar, "", 0);
    EXPECT_EQ(jar, prev);
    free(jar);
}

TEST(VigilHttpTest, HdrNameMatches)
{
    EXPECT_EQ(hdr_name_matches("Set-Cookie: a=1", "set-cookie", 10), 1);
    EXPECT_EQ(hdr_name_matches("set-cookie: b=2", "set-cookie", 10), 1);
    EXPECT_EQ(hdr_name_matches("Content-Type: text/plain", "set-cookie", 10), 0);
    EXPECT_EQ(hdr_name_matches("Short", "set-cookie", 10), 0);
}

TEST(VigilHttpTest, CollectCookiesFromHeaders)
{
    const char *hdrs = "HTTP/1.1 200 OK\r\n"
                       "Set-Cookie: session=abc\r\n"
                       "Content-Type: text/plain\r\n"
                       "Set-Cookie: user=bob; Path=/; HttpOnly\r\n"
                       "\r\n";
    char *jar = NULL;
    collect_cookies(hdrs, &jar);
    EXPECT_TRUE(jar != NULL);
    EXPECT_TRUE(strstr(jar, "session=abc") != NULL);
    EXPECT_TRUE(strstr(jar, "user=bob") != NULL);
    /* Directive stripped — no "Path" in jar */
    EXPECT_EQ(strstr(jar, "Path"), NULL);
    free(jar);
}

TEST(VigilHttpTest, CollectCookiesEmpty)
{
    char *jar = NULL;
    collect_cookies(NULL, &jar);
    EXPECT_EQ(jar, NULL);
    collect_cookies("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", &jar);
    EXPECT_EQ(jar, NULL); /* no Set-Cookie headers */
}

TEST(VigilHttpTest, BuildRequestHeadersWithCookies)
{
    char *hdrs = build_request_headers("session=abc", "X-Custom: val\r\n");
    ASSERT_TRUE(hdrs != NULL);
    EXPECT_TRUE(strstr(hdrs, "Cookie: session=abc") != NULL);
    EXPECT_TRUE(strstr(hdrs, "X-Custom: val") != NULL);
    free(hdrs);
}

TEST(VigilHttpTest, BuildRequestHeadersNoCookies)
{
    /* NULL or empty cookie jar returns NULL */
    EXPECT_EQ(build_request_headers(NULL, "X-Header: foo\r\n"), NULL);
    EXPECT_EQ(build_request_headers("", "X-Header: foo\r\n"), NULL);
}

TEST(VigilHttpTest, RedirectChangesMethodTrue)
{
    EXPECT_EQ(redirect_changes_method(301), 1);
    EXPECT_EQ(redirect_changes_method(302), 1);
    EXPECT_EQ(redirect_changes_method(303), 1);
}

TEST(VigilHttpTest, RedirectChangesMethodFalse)
{
    EXPECT_EQ(redirect_changes_method(307), 0);
    EXPECT_EQ(redirect_changes_method(308), 0);
    EXPECT_EQ(redirect_changes_method(200), 0);
}

/* ── do_request loopback tests ───────────────────────────────────── */

#define DO_REQ_PORT 18792
#define DO_REQ_REDIR_PORT 18793
#define DO_REQ_DEST_PORT 18794

TEST(VigilHttpTest, DoRequestLoopback)
{
    test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    const char *canned = "HTTP/1.1 200 OK\r\n"
                         "Set-Cookie: tok=xyz\r\n"
                         "Content-Length: 5\r\n"
                         "\r\n"
                         "hello";

    if (!start_test_server_port(&srv, canned, DO_REQ_PORT, &thr))
        return;

    http_response_t resp;
    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/test", DO_REQ_PORT);
    int rc = do_request("GET", url, NULL, NULL, 0, &resp);
    EXPECT_EQ(rc, 0);
    if (rc == 0)
    {
        EXPECT_EQ(resp.status_code, 200);
        response_free(&resp);
    }
    stop_test_server(&srv, thr);
}

TEST(VigilHttpTest, DoRequestFollowsRedirect)
{
    test_server_t srv_redir, srv_dest;
    vigil_platform_thread_t *thr_redir = NULL, *thr_dest = NULL;

    char redir_body[256];
    snprintf(redir_body, sizeof(redir_body),
             "HTTP/1.1 302 Found\r\n"
             "Location: http://127.0.0.1:%d/dest\r\n"
             "Set-Cookie: redir=1\r\n"
             "Content-Length: 0\r\n"
             "\r\n",
             DO_REQ_DEST_PORT);

    const char *dest_resp = "HTTP/1.1 200 OK\r\n"
                            "Content-Length: 2\r\n"
                            "\r\n"
                            "ok";

    if (!start_test_server_port(&srv_redir, redir_body, DO_REQ_REDIR_PORT, &thr_redir))
        return;
    if (!start_test_server_port(&srv_dest, dest_resp, DO_REQ_DEST_PORT, &thr_dest))
    {
        stop_test_server(&srv_redir, thr_redir);
        return;
    }

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/start", DO_REQ_REDIR_PORT);

    http_response_t resp;
    int rc = do_request("POST", url, NULL, "data", 4, &resp);
    EXPECT_EQ(rc, 0);
    if (rc == 0)
    {
        EXPECT_EQ(resp.status_code, 200);
        response_free(&resp);
    }

    stop_test_server(&srv_redir, thr_redir);
    stop_test_server(&srv_dest, thr_dest);
}

TEST(VigilHttpTest, DoRequestHttpsFallbackFails)
{
    /* Without libcurl, HTTPS via do_request should fail gracefully */
    http_response_t resp;
    int rc = do_request("GET", "https://127.0.0.1:18789/secure", NULL, NULL, 0, &resp);
    /* Either libcurl handles it or it returns -1 (no TLS fallback) */
    if (rc != 0)
    {
        EXPECT_EQ(rc, -1);
    }
    else
    {
        response_free(&resp);
    }
}

/* ── Additional URL parsing edge cases ───────────────────────────── */

TEST(VigilHttpTest, ParseUrlSchemeTooLong)
{
    /* Scheme buffer is 16 bytes; a 20-char scheme must be rejected. */
    parsed_url_t u;
    EXPECT_EQ(parse_url("averylongschemename://host/path", &u), 0);
}

TEST(VigilHttpTest, ParseUrlHostTooLong)
{
    /* Host buffer is 256 bytes; 256-char hostname must be rejected. */
    char url[512];
    memcpy(url, "http://", 7);
    memset(url + 7, 'a', 256);
    memcpy(url + 7 + 256, "/path", 6);

    parsed_url_t u;
    EXPECT_EQ(parse_url(url, &u), 0);
}

/* ── parse_http_response happy path ──────────────────────────────── */

TEST(VigilHttpTest, ParseHttpResponseValid)
{
    /* Full response: verify status, headers, and body are all parsed. */
    char buf[] = "HTTP/1.1 201 Created\r\n"
                 "Content-Type: text/plain\r\n"
                 "Content-Length: 5\r\n"
                 "\r\n"
                 "hello";
    http_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = parse_http_response(buf, sizeof(buf) - 1, &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 201);
    EXPECT_STREQ(resp.body, "hello"); /* EXPECT_STREQ fails safely on NULL */
    response_free(&resp);
}

TEST(VigilHttpTest, ParseHttpResponseNoBody)
{
    /* Empty body — body_len == 0 and body is a valid empty string. */
    char buf[] = "HTTP/1.1 204 No Content\r\n"
                 "Content-Length: 0\r\n"
                 "\r\n";
    http_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = parse_http_response(buf, sizeof(buf) - 1, &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 204);
    EXPECT_EQ(resp.body_len, 0u);
    response_free(&resp);
}

/* ── Cookie / header helper edge cases ───────────────────────────── */

TEST(VigilHttpTest, CookieJarAppendTrailingWhitespace)
{
    /* Value that is only whitespace trims to zero length — no-op. */
    char *jar = NULL;
    cookie_jar_append(&jar, "   ", 3);
    EXPECT_EQ(jar, NULL);

    /* After a real cookie, appending whitespace leaves jar unchanged. */
    cookie_jar_append(&jar, "x=1", 3);
    char *prev = jar;
    cookie_jar_append(&jar, "  \t ", 4);
    EXPECT_EQ(jar, prev); /* pointer unchanged */
    free(jar);
}

TEST(VigilHttpTest, CollectCookiesNoTrailingCrlf)
{
    /* Last header line has no CRLF — eol falls back to strlen path. */
    const char *hdrs = "HTTP/1.1 200 OK\r\n"
                       "Set-Cookie: last=val";
    char *jar = NULL;
    collect_cookies(hdrs, &jar);
    EXPECT_NE(jar, NULL);
    if (jar)
        EXPECT_STRNE(strstr(jar, "last=val"), NULL);
    free(jar);
}

TEST(VigilHttpTest, BuildRequestHeadersCookieOnly)
{
    /* NULL existing headers — output contains only the Cookie line. */
    char *hdrs = build_request_headers("tok=abc", NULL);
    ASSERT_NE(hdrs, NULL);
    EXPECT_STREQ(hdrs, "Cookie: tok=abc\r\n");
    free(hdrs);
}

/* ── do_request: 307 preserves method ───────────────────────────── */

#define REDIR_307_PORT 18795
#define DEST_307_PORT 18796

typedef struct
{
    vigil_socket_t listener;
    volatile int ready;
    int port;
    char received_method[32];
    char received_body[256];
    const char *response;
} method_capture_server_t;

static void method_capture_func(void *arg)
{
    method_capture_server_t *srv = (method_capture_server_t *)arg;
    if (vigil_platform_tcp_listen("127.0.0.1", srv->port, &srv->listener, NULL) != VIGIL_STATUS_OK)
        return;
    srv->ready = 1;

    vigil_socket_t client = VIGIL_INVALID_SOCKET;
    if (vigil_platform_tcp_accept(srv->listener, &client, NULL) != VIGIL_STATUS_OK)
        return;

    char buf[4096];
    size_t n = 0;
    vigil_platform_tcp_recv(client, buf, sizeof(buf) - 1, &n, NULL);
    buf[n] = '\0';

    char *sp = strchr(buf, ' ');
    if (sp)
    {
        size_t mlen = (size_t)(sp - buf);
        if (mlen < sizeof(srv->received_method))
        {
            memcpy(srv->received_method, buf, mlen);
            srv->received_method[mlen] = '\0';
        }
    }
    char *body_start = strstr(buf, "\r\n\r\n");
    if (body_start)
    {
        body_start += 4;
        size_t blen = strlen(body_start);
        if (blen < sizeof(srv->received_body))
        {
            memcpy(srv->received_body, body_start, blen);
            srv->received_body[blen] = '\0';
        }
    }

    vigil_platform_tcp_send(client, srv->response, strlen(srv->response), NULL, NULL);
    vigil_platform_tcp_close(client, NULL);
}

TEST(VigilHttpTest, DoRequest307PreservesMethod)
{
    /* 307 Temporary Redirect must NOT change POST→GET. */
    char redir_body[256];
    snprintf(redir_body, sizeof(redir_body),
             "HTTP/1.1 307 Temporary Redirect\r\n"
             "Location: http://127.0.0.1:%d/dest\r\n"
             "Content-Length: 0\r\n"
             "\r\n",
             DEST_307_PORT);

    test_server_t srv_redir;
    vigil_platform_thread_t *thr_redir = NULL;
    if (!start_test_server_port(&srv_redir, redir_body, REDIR_307_PORT, &thr_redir))
        return;

    method_capture_server_t srv_dest;
    memset(&srv_dest, 0, sizeof(srv_dest));
    srv_dest.port = DEST_307_PORT;
    srv_dest.response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";

    vigil_platform_thread_t *thr_dest = NULL;
    vigil_platform_net_init(NULL);
    if (vigil_platform_thread_create(&thr_dest, method_capture_func, &srv_dest, NULL) != VIGIL_STATUS_OK)
    {
        stop_test_server(&srv_redir, thr_redir);
        return;
    }
    for (int i = 0; i < 200 && !srv_dest.ready; i++)
        vigil_platform_thread_sleep(10);
    if (!srv_dest.ready)
    {
        stop_test_server(&srv_redir, thr_redir);
        vigil_platform_thread_join(thr_dest, NULL);
        return;
    }

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/start", REDIR_307_PORT);
    http_response_t resp;
    int rc = do_request("POST", url, NULL, "payload", 7, &resp);
    if (rc == 0)
        response_free(&resp);

    stop_test_server(&srv_redir, thr_redir);
    if (srv_dest.listener != VIGIL_INVALID_SOCKET)
        vigil_platform_tcp_close(srv_dest.listener, NULL);
    vigil_platform_thread_join(thr_dest, NULL);

    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(srv_dest.received_method, "POST");
}

/* ── do_request: Location header too long to follow ─────────────── */

#define LONG_LOC_PORT 18797

static char g_long_loc_response[6000];

TEST(VigilHttpTest, DoRequestRedirectLocationTooLong)
{
    /* Location value > 4095 bytes: find_location_header returns NULL,
     * the redirect is silently dropped, and the 302 is returned as-is. */
    memcpy(g_long_loc_response, "HTTP/1.1 302 Found\r\nLocation: http://", 37);
    memset(g_long_loc_response + 37, 'x', 4200);
    memcpy(g_long_loc_response + 37 + 4200, "/\r\nContent-Length: 0\r\n\r\n", 24);
    g_long_loc_response[37 + 4200 + 24] = '\0';

    test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    if (!start_test_server_port(&srv, g_long_loc_response, LONG_LOC_PORT, &thr))
        return;

    http_response_t resp;
    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/test", LONG_LOC_PORT);
    int rc = do_request("GET", url, NULL, NULL, 0, &resp);
    /* On Linux the socket fallback receives the 302 directly and returns
     * it as-is (location too long to follow → find_location_header NULL).
     * On Windows WinHTTP handles the redirect natively, fails DNS on the
     * bogus hostname, and rc may be -1.  Accept either outcome; the key
     * invariant is that the call does not crash or hang. */
    if (rc == 0)
    {
        EXPECT_EQ(resp.status_code, 302);
        response_free(&resp);
    }

    stop_test_server(&srv, thr);
}

/* ── Response free safety ────────────────────────────────────────── */

TEST(VigilHttpTest, ResponseFreeNull)
{
    http_response_t resp;
    memset(&resp, 0, sizeof(resp));
    response_free(&resp); /* should not crash */
    EXPECT_EQ(resp.body, NULL);
    EXPECT_EQ(resp.headers, NULL);
}

TEST(VigilHttpTest, ParseHttpResponseMalformed)
{
    /* Buffer with no \r\n\r\n separator — parse must return -1. */
    char buf[] = "HTTP/1.1 200 OK\r\nContent-Length: 0";
    http_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = parse_http_response(buf, sizeof(buf) - 1, &resp);
    EXPECT_EQ(rc, -1);
    EXPECT_EQ(resp.headers, NULL);
    EXPECT_EQ(resp.body, NULL);
}

/* ── Server round-trip tests ──────────────────────────────────────── */

#define SERVER_TEST_PORT 18788

typedef struct
{
    int port;
    volatile int ready;
    char received_method[32];
    char received_path[256];
    char received_body[1024];
} server_test_ctx_t;

static void server_thread_func(void *arg)
{
    server_test_ctx_t *ctx = (server_test_ctx_t *)arg;

    vigil_socket_t listener = VIGIL_INVALID_SOCKET;
    if (vigil_platform_tcp_listen("127.0.0.1", ctx->port, &listener, NULL) != VIGIL_STATUS_OK)
        return;

    ctx->ready = 1;

    vigil_socket_t client = VIGIL_INVALID_SOCKET;
    if (vigil_platform_tcp_accept(listener, &client, NULL) != VIGIL_STATUS_OK)
    {
        vigil_platform_tcp_close(listener, NULL);
        return;
    }

    /* Read request */
    char buf[4096];
    size_t len = 0;
    for (;;)
    {
        size_t n = 0;
        if (vigil_platform_tcp_recv(client, buf + len, sizeof(buf) - len - 1, &n, NULL) != VIGIL_STATUS_OK || n == 0)
            break;
        len += n;
        buf[len] = '\0';
        if (strstr(buf, "\r\n\r\n"))
            break;
    }

    /* Parse method and path */
    char *sp1 = strchr(buf, ' ');
    if (sp1)
    {
        size_t mlen = (size_t)(sp1 - buf);
        if (mlen < sizeof(ctx->received_method))
        {
            memcpy(ctx->received_method, buf, mlen);
            ctx->received_method[mlen] = '\0';
        }
        char *sp2 = strchr(sp1 + 1, ' ');
        if (sp2)
        {
            size_t plen = (size_t)(sp2 - sp1 - 1);
            if (plen < sizeof(ctx->received_path))
            {
                memcpy(ctx->received_path, sp1 + 1, plen);
                ctx->received_path[plen] = '\0';
            }
        }
    }

    /* Extract body after \r\n\r\n */
    char *body_start = strstr(buf, "\r\n\r\n");
    if (body_start)
    {
        body_start += 4;
        size_t blen = len - (size_t)(body_start - buf);
        if (blen < sizeof(ctx->received_body))
        {
            memcpy(ctx->received_body, body_start, blen);
            ctx->received_body[blen] = '\0';
        }
    }

    /* Send response */
    const char *response = "HTTP/1.1 200 OK\r\n"
                           "Content-Length: 11\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "hello world";
    vigil_platform_tcp_send(client, response, strlen(response), NULL, NULL);

    vigil_platform_tcp_close(client, NULL);
    vigil_platform_tcp_close(listener, NULL);
}

TEST(VigilHttpTest, ServerRoundTrip)
{
    server_test_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.port = SERVER_TEST_PORT;

    vigil_platform_thread_t *thr = NULL;
    vigil_status_t st = vigil_platform_thread_create(&thr, server_thread_func, &ctx, NULL);
    if (st != VIGIL_STATUS_OK)
        return;

    for (int i = 0; i < 200 && !ctx.ready; i++)
        vigil_platform_thread_sleep(10);
    if (!ctx.ready)
    {
        vigil_platform_thread_join(thr, NULL);
        return;
    }

    /* Use socket_request to talk to our server */
    parsed_url_t url;
    parse_url("http://127.0.0.1:18788/test/path", &url);

    http_response_t resp;
    int rc = socket_request("GET", &url, NULL, NULL, 0, &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 200);
    EXPECT_STREQ(resp.body, "hello world");
    response_free(&resp);

    vigil_platform_thread_join(thr, NULL);

    /* Verify the server received the right request */
    EXPECT_STREQ(ctx.received_method, "GET");
    EXPECT_TRUE(strcmp(ctx.received_path, "/test/path") == 0);
}

TEST(VigilHttpTest, ServerPostRoundTrip)
{
    server_test_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.port = SERVER_TEST_PORT + 1;

    vigil_platform_thread_t *thr = NULL;
    vigil_status_t st = vigil_platform_thread_create(&thr, server_thread_func, &ctx, NULL);
    if (st != VIGIL_STATUS_OK)
        return;

    for (int i = 0; i < 200 && !ctx.ready; i++)
        vigil_platform_thread_sleep(10);
    if (!ctx.ready)
    {
        vigil_platform_thread_join(thr, NULL);
        return;
    }

    parsed_url_t url;
    parse_url("http://127.0.0.1:18789/submit", &url);

    const char *body = "test data";
    http_response_t resp;
    int rc = socket_request("POST", &url, "Content-Type: text/plain\r\n", body, strlen(body), &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 200);
    response_free(&resp);

    vigil_platform_thread_join(thr, NULL);

    EXPECT_STREQ(ctx.received_method, "POST");
    EXPECT_TRUE(strcmp(ctx.received_path, "/submit") == 0);
}

/* ── BearSSL TLS loopback tests ──────────────────────────────────── */

#if defined(VIGIL_ENABLE_BEARSSL_TLS) && defined(VIGIL_TLS_TEST_CERT_AVAILABLE)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244 4267)
#endif
#include "bearssl.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include "http_tls_test_cert.h"

#define TLS_TEST_PORT 18790

/* Socket callbacks reused by the in-process TLS test server. */
static int tls_srv_read_cb(void *ctx, unsigned char *buf, size_t len)
{
    vigil_socket_t sock = *(vigil_socket_t *)ctx;
    size_t n = 0;
    vigil_status_t st = vigil_platform_tcp_recv(sock, buf, len, &n, NULL);
    return (st != VIGIL_STATUS_OK || n == 0) ? -1 : (int)n;
}

static int tls_srv_write_cb(void *ctx, const unsigned char *buf, size_t len)
{
    vigil_socket_t sock = *(vigil_socket_t *)ctx;
    size_t n = 0;
    return vigil_platform_tcp_send(sock, buf, len, &n, NULL) != VIGIL_STATUS_OK ? -1 : (int)n;
}

typedef struct
{
    vigil_socket_t listener;
    volatile int ready;
    const char *response;
    int port;
    br_skey_decoder_context kctx; /* keeps private key memory alive */
} tls_test_server_t;

static void tls_server_func(void *arg)
{
    tls_test_server_t *srv = (tls_test_server_t *)arg;

    if (vigil_platform_tcp_listen("127.0.0.1", srv->port, &srv->listener, NULL) != VIGIL_STATUS_OK)
        return;
    srv->ready = 1;

    vigil_socket_t client = VIGIL_INVALID_SOCKET;
    if (vigil_platform_tcp_accept(srv->listener, &client, NULL) != VIGIL_STATUS_OK)
        return;

    br_skey_decoder_init(&srv->kctx);
    br_skey_decoder_push(&srv->kctx, vigil_test_tls_key_der, vigil_test_tls_key_der_len);
    if (br_skey_decoder_last_error(&srv->kctx) != 0 || br_skey_decoder_key_type(&srv->kctx) != BR_KEYTYPE_EC)
    {
        vigil_platform_tcp_close(client, NULL);
        return;
    }
    const br_ec_private_key *sk = br_skey_decoder_get_ec(&srv->kctx);

    br_x509_certificate chain[1];
    chain[0].data = (unsigned char *)(uintptr_t)vigil_test_tls_cert_der;
    chain[0].data_len = vigil_test_tls_cert_der_len;

    br_ssl_server_context sc;
    /* minf2g: ECDHE_ECDSA with AES-128-GCM-SHA256 — matches our EC P-256 key. */
    br_ssl_server_init_minf2g(&sc, chain, 1, sk);

    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    br_ssl_engine_set_buffer(&sc.eng, iobuf, sizeof(iobuf), 1);
    br_ssl_server_reset(&sc);

    br_sslio_context ioc;
    br_sslio_init(&ioc, &sc.eng, tls_srv_read_cb, &client, tls_srv_write_cb, &client);

    /* Drain the request (enough to unblock the client write). */
    char req[4096];
    br_sslio_read(&ioc, req, sizeof(req) - 1);

    br_sslio_write_all(&ioc, srv->response, strlen(srv->response));
    br_sslio_flush(&ioc);
    br_sslio_close(&ioc);
    vigil_platform_tcp_close(client, NULL);
}

static int start_tls_server(tls_test_server_t *srv, const char *response, int port, vigil_platform_thread_t **thread)
{
    memset(srv, 0, sizeof(*srv));
    srv->listener = VIGIL_INVALID_SOCKET;
    srv->response = response;
    srv->port = port;

    vigil_platform_net_init(NULL);
    vigil_status_t st = vigil_platform_thread_create(thread, tls_server_func, srv, NULL);
    if (st != VIGIL_STATUS_OK)
        return 0;

    for (int i = 0; i < 200 && !srv->ready; i++)
        vigil_platform_thread_sleep(10);
    return srv->ready;
}

static void stop_tls_server(tls_test_server_t *srv, vigil_platform_thread_t *thread)
{
    if (srv->listener != VIGIL_INVALID_SOCKET)
        vigil_platform_tcp_close(srv->listener, NULL);
    vigil_platform_thread_join(thread, NULL);
}

/* Client-side: verify the BearSSL TLS transport carries a plain HTTP GET. */
TEST(VigilHttpTest, BearSslHttpsGet)
{
    const char *canned = "HTTP/1.1 200 OK\r\n"
                         "Content-Length: 2\r\n"
                         "Connection: close\r\n"
                         "\r\nOK";
    tls_test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    if (!start_tls_server(&srv, canned, TLS_TEST_PORT, &thr))
        return;

    parsed_url_t url;
    char url_str[64];
    snprintf(url_str, sizeof(url_str), "https://127.0.0.1:%d/hello", TLS_TEST_PORT);
    parse_url(url_str, &url);

    http_response_t resp;
    int rc = bearssl_https_insecure_request("GET", &url, NULL, NULL, 0, &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 200);
    response_free(&resp);

    stop_tls_server(&srv, thr);
}

/* Verify BearSSL TLS carries a POST with a request body. */
TEST(VigilHttpTest, BearSslHttpsPost)
{
    const char *canned = "HTTP/1.1 201 Created\r\n"
                         "Content-Length: 0\r\n"
                         "Connection: close\r\n"
                         "\r\n";
    tls_test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    if (!start_tls_server(&srv, canned, TLS_TEST_PORT + 1, &thr))
        return;

    parsed_url_t url;
    char url_str[64];
    snprintf(url_str, sizeof(url_str), "https://127.0.0.1:%d/upload", TLS_TEST_PORT + 1);
    parse_url(url_str, &url);

    const char *body = "payload";
    http_response_t resp;
    int rc = bearssl_https_insecure_request("POST", &url, "Content-Type: text/plain\r\n", body, strlen(body), &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.status_code, 201);
    response_free(&resp);

    stop_tls_server(&srv, thr);
}

#endif /* VIGIL_ENABLE_BEARSSL_TLS && VIGIL_TLS_TEST_CERT_AVAILABLE */

/* ── parse_request_line tests ─────────────────────────────────────── */

TEST(VigilHttpTest, ParseRequestLineBasic)
{
    char buf[] = "GET /index.html HTTP/1.1\r\n";
    const char *line_end = buf + strlen("GET /index.html HTTP/1.1");
    char *method = NULL, *path = NULL;
    int rc = parse_request_line(buf, line_end, &method, &path);
    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(method && path && strcmp(method, "GET") == 0 && strcmp(path, "/index.html") == 0);
    free(method);
    free(path);
}

TEST(VigilHttpTest, ParseRequestLineMissingPath)
{
    /* No second space — must fail. */
    char buf[] = "GET\r\n";
    const char *line_end = buf + strlen("GET");
    char *method = NULL, *path = NULL;
    int rc = parse_request_line(buf, line_end, &method, &path);
    EXPECT_EQ(rc, -1);
}

TEST(VigilHttpTest, ParseRequestLineMissingHttpVersion)
{
    /* Space after method but no second space before HTTP version. */
    char buf[] = "POST /submit\r\n";
    const char *line_end = buf + strlen("POST /submit");
    char *method = NULL, *path = NULL;
    int rc = parse_request_line(buf, line_end, &method, &path);
    EXPECT_EQ(rc, -1);
}

/* ── parse_content_length tests ───────────────────────────────────── */

TEST(VigilHttpTest, ParseContentLengthPresent)
{
    size_t out = 0;
    int rc = parse_content_length("Content-Length: 42\r\nAccept: */*\r\n", &out);
    EXPECT_EQ(rc, 1);
    EXPECT_EQ(out, 42u);
}

TEST(VigilHttpTest, ParseContentLengthAbsent)
{
    size_t out = 99;
    int rc = parse_content_length("Accept: */*\r\n", &out);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(out, 0u);
}

TEST(VigilHttpTest, ParseContentLengthLowercase)
{
    size_t out = 0;
    int rc = parse_content_length("content-length: 7\r\n", &out);
    EXPECT_EQ(rc, 1);
    EXPECT_EQ(out, 7u);
}

/* ── ensure_hdr_capacity tests ────────────────────────────────────── */

TEST(VigilHttpTest, EnsureHdrCapacityNoGrowthNeeded)
{
    /* buf has plenty of room — returns same pointer, cap unchanged. */
    size_t cap = 128;
    char *buf = (char *)malloc(cap);
    ASSERT_NE(buf, NULL);
    char *result = ensure_hdr_capacity(buf, &cap, 10);
    EXPECT_EQ(result, buf);
    EXPECT_EQ(cap, 128u);
    free(result);
}

TEST(VigilHttpTest, EnsureHdrCapacityGrows)
{
    /* len+1 == cap triggers a doubling. */
    size_t cap = 16;
    char *buf = (char *)malloc(cap);
    ASSERT_NE(buf, NULL);
    char *result = ensure_hdr_capacity(buf, &cap, 15); /* len+1 == 16 == cap */
    EXPECT_NE(result, NULL);
    EXPECT_EQ(cap, 32u);
    free(result);
}

/* ── parse_incoming_request loopback tests ────────────────────────── */

#define PARSE_INCOMING_PORT 18800

typedef struct
{
    int port;
    const char *request;
    volatile int ready;
} incoming_sender_ctx_t;

static void incoming_sender_func(void *arg)
{
    incoming_sender_ctx_t *ctx = (incoming_sender_ctx_t *)arg;
    vigil_socket_t listener = VIGIL_INVALID_SOCKET;
    if (vigil_platform_tcp_listen("127.0.0.1", ctx->port, &listener, NULL) != VIGIL_STATUS_OK)
        return;
    ctx->ready = 1;

    vigil_socket_t client = VIGIL_INVALID_SOCKET;
    if (vigil_platform_tcp_accept(listener, &client, NULL) != VIGIL_STATUS_OK)
    {
        vigil_platform_tcp_close(listener, NULL);
        return;
    }
    vigil_platform_tcp_send(client, ctx->request, strlen(ctx->request), NULL, NULL);
    vigil_platform_tcp_close(client, NULL);
    vigil_platform_tcp_close(listener, NULL);
}

/* ctx must be caller-owned (stack or otherwise) and outlive the joined thread. */
static vigil_socket_t connect_to_incoming_sender(incoming_sender_ctx_t *ctx, int port, const char *request,
                                                 vigil_platform_thread_t **thr_out)
{
    ctx->port = port;
    ctx->request = request;
    ctx->ready = 0;

    vigil_platform_net_init(NULL);
    if (vigil_platform_thread_create(thr_out, incoming_sender_func, ctx, NULL) != VIGIL_STATUS_OK)
        return VIGIL_INVALID_SOCKET;
    for (int i = 0; i < 200 && !ctx->ready; i++)
        vigil_platform_thread_sleep(10);
    if (!ctx->ready)
        return VIGIL_INVALID_SOCKET;

    vigil_socket_t sock = VIGIL_INVALID_SOCKET;
    vigil_platform_tcp_connect("127.0.0.1", port, &sock, NULL);
    return sock;
}

TEST(VigilHttpTest, ParseIncomingRequestGet)
{
    const char *req = "GET /hello HTTP/1.1\r\n"
                      "Host: localhost\r\n"
                      "\r\n";
    incoming_sender_ctx_t ctx;
    vigil_platform_thread_t *thr = NULL;
    vigil_socket_t sock = connect_to_incoming_sender(&ctx, PARSE_INCOMING_PORT, req, &thr);
    if (sock == VIGIL_INVALID_SOCKET)
        return;

    http_conn_t conn;
    memset(&conn, 0, sizeof(conn));
    conn.sock = sock;
    int rc = parse_incoming_request(&conn);
    vigil_platform_tcp_close(sock, NULL);
    vigil_platform_thread_join(thr, NULL);

    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(conn.method && conn.path && strcmp(conn.method, "GET") == 0 && strcmp(conn.path, "/hello") == 0);
    EXPECT_EQ(conn.body_len, 0u);
    free(conn.method);
    free(conn.path);
    free(conn.headers);
    free(conn.body);
}

TEST(VigilHttpTest, ParseIncomingRequestPost)
{
    const char *req = "POST /submit HTTP/1.1\r\n"
                      "Content-Length: 9\r\n"
                      "\r\n"
                      "test_body";
    incoming_sender_ctx_t ctx;
    vigil_platform_thread_t *thr = NULL;
    vigil_socket_t sock = connect_to_incoming_sender(&ctx, PARSE_INCOMING_PORT + 1, req, &thr);
    if (sock == VIGIL_INVALID_SOCKET)
        return;

    http_conn_t conn;
    memset(&conn, 0, sizeof(conn));
    conn.sock = sock;
    int rc = parse_incoming_request(&conn);
    vigil_platform_tcp_close(sock, NULL);
    vigil_platform_thread_join(thr, NULL);

    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(conn.method && conn.path && conn.body && strcmp(conn.method, "POST") == 0 &&
                strcmp(conn.path, "/submit") == 0 && strcmp(conn.body, "test_body") == 0);
    EXPECT_EQ(conn.body_len, 9u);
    free(conn.method);
    free(conn.path);
    free(conn.headers);
    free(conn.body);
}

TEST(VigilHttpTest, ParseIncomingRequestMalformed)
{
    /* No request-line spaces — must return -1. */
    const char *req = "BADREQUEST\r\n\r\n";
    incoming_sender_ctx_t ctx;
    vigil_platform_thread_t *thr = NULL;
    vigil_socket_t sock = connect_to_incoming_sender(&ctx, PARSE_INCOMING_PORT + 2, req, &thr);
    if (sock == VIGIL_INVALID_SOCKET)
        return;

    http_conn_t conn;
    memset(&conn, 0, sizeof(conn));
    conn.sock = sock;
    int rc = parse_incoming_request(&conn);
    vigil_platform_tcp_close(sock, NULL);
    vigil_platform_thread_join(thr, NULL);

    EXPECT_EQ(rc, -1);
}

/* ── find_location_header tests ───────────────────────────────────── */

TEST(VigilHttpTest, FindLocationHeaderBasic)
{
    char buf[1024];
    const char *hdrs = "HTTP/1.1 302 Found\r\n"
                       "Location: http://example.com/new\r\n"
                       "Content-Length: 0\r\n";
    const char *loc = find_location_header(hdrs, buf, sizeof(buf));
    EXPECT_STREQ(loc, "http://example.com/new");
}

TEST(VigilHttpTest, FindLocationHeaderLowercase)
{
    /* Header name in all-lowercase must still match. */
    char buf[1024];
    const char *hdrs = "HTTP/1.1 302 Found\r\n"
                       "location: http://example.com/lower\r\n";
    const char *loc = find_location_header(hdrs, buf, sizeof(buf));
    EXPECT_STREQ(loc, "http://example.com/lower");
}

TEST(VigilHttpTest, FindLocationHeaderLeadingSpaces)
{
    /* Leading spaces after the colon must be stripped. */
    char buf[1024];
    const char *hdrs = "HTTP/1.1 302 Found\r\n"
                       "Location:   http://example.com/spaced\r\n";
    const char *loc = find_location_header(hdrs, buf, sizeof(buf));
    EXPECT_STREQ(loc, "http://example.com/spaced");
}

TEST(VigilHttpTest, FindLocationHeaderNull)
{
    char buf[64];
    EXPECT_EQ(find_location_header(NULL, buf, sizeof(buf)), NULL);
}

TEST(VigilHttpTest, FindLocationHeaderValueTooLong)
{
    /* buf is smaller than the value — must return NULL. */
    char buf[10];
    const char *hdrs = "Location: http://example.com/path/that/is/quite/long\r\n";
    EXPECT_EQ(find_location_header(hdrs, buf, sizeof(buf)), NULL);
}

TEST(VigilHttpTest, FindLocationHeaderNotPresent)
{
    char buf[1024];
    const char *hdrs = "Content-Type: text/html\r\nContent-Length: 5\r\n";
    EXPECT_EQ(find_location_header(hdrs, buf, sizeof(buf)), NULL);
}

/* ── route_matches tests ──────────────────────────────────────────── */

TEST(VigilHttpTest, RouteMatchesExact)
{
    EXPECT_EQ(route_matches("/foo", "/foo"), 1);
    EXPECT_EQ(route_matches("/foo", "/bar"), 0);
    EXPECT_EQ(route_matches("/", "/"), 1);
}

TEST(VigilHttpTest, RouteMatchesPrefix)
{
    /* Pattern ending with '/' matches any path that starts with it. */
    EXPECT_EQ(route_matches("/api/", "/api/v1"), 1);
    EXPECT_EQ(route_matches("/api/", "/api/"), 1);
    EXPECT_EQ(route_matches("/api/", "/other/v1"), 0);
}

TEST(VigilHttpTest, RouteMatchesQueryStripped)
{
    /* Query string in the path must not prevent a match. */
    EXPECT_EQ(route_matches("/search", "/search?q=hello"), 1);
    EXPECT_EQ(route_matches("/search", "/other?q=hello"), 0);
}

TEST(VigilHttpTest, RouteMatchesNull)
{
    EXPECT_EQ(route_matches(NULL, "/foo"), 0);
    EXPECT_EQ(route_matches("/foo", NULL), 0);
}

/* ── collect_cookies with attributes ────────────────────────────────  */

TEST(VigilHttpTest, CollectCookiesWithAttributes)
{
    /* Only name=value is kept; Path, HttpOnly, Secure etc. are stripped. */
    const char *hdrs = "Set-Cookie: session=abc123; Path=/; HttpOnly\r\n"
                       "Set-Cookie: user=bob; Secure\r\n";
    char *jar = NULL;
    collect_cookies(hdrs, &jar);
    EXPECT_STREQ(jar, "session=abc123; user=bob");
    free(jar);
}

/* ── do_request: 304 must not be followed as a redirect ──────────── */

#define DO_REQ_304_PORT 18796

TEST(VigilHttpTest, DoRequest304NotRedirected)
{
    test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    const char *canned = "HTTP/1.1 304 Not Modified\r\n"
                         "Content-Length: 0\r\n"
                         "\r\n";
    if (!start_test_server_port(&srv, canned, DO_REQ_304_PORT, &thr))
        return;

    http_response_t resp;
    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/check", DO_REQ_304_PORT);
    int rc = do_request("GET", url, NULL, NULL, 0, &resp);
    EXPECT_EQ(rc, 0);
    if (rc == 0)
    {
        EXPECT_EQ(resp.status_code, 304);
        response_free(&resp);
    }
    stop_test_server(&srv, thr);
}

/* ── parse_content_length: value too large ───────────────────────── */

TEST(VigilHttpTest, ParseContentLengthTooLarge)
{
    /* Value exceeding HTTP_MAX_BODY_BYTES must return -1. */
    size_t out = 0;
    int rc = parse_content_length("Content-Length: 99999999999\r\n", &out);
    EXPECT_EQ(rc, -1);
}

/* ── recv_body_bytes: pre-buffered body (no socket needed) ─────────  */

TEST(VigilHttpTest, RecvBodyBytesPreBuffered)
{
    /* already == content_length: data is already in buffer, no recv needed. */
    const char bstart[] = "hello";
    char body[16];
    memset(body, 0, sizeof(body));
    int rc = recv_body_bytes(VIGIL_INVALID_SOCKET, body, bstart, 5, 5);
    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(memcmp(body, "hello", 5) == 0);
}

/* ── recv_body_bytes: reads from socket ───────────────────────────── */

#define RECV_BODY_BYTES_PORT 18803

TEST(VigilHttpTest, RecvBodyBytesFromSocket)
{
    incoming_sender_ctx_t ctx;
    vigil_platform_thread_t *thr = NULL;
    vigil_socket_t sock = connect_to_incoming_sender(&ctx, RECV_BODY_BYTES_PORT, "world", &thr);
    if (sock == VIGIL_INVALID_SOCKET)
        return;

    char body[16];
    memset(body, 0, sizeof(body));
    int rc = recv_body_bytes(sock, body, NULL, 0, 5);
    vigil_platform_tcp_close(sock, NULL);
    vigil_platform_thread_join(thr, NULL);

    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(memcmp(body, "world", 5) == 0);
}

/* ── recv_request_body: calls recv_body_bytes via socket ──────────── */

#define RECV_REQUEST_BODY_PORT 18804

TEST(VigilHttpTest, RecvRequestBodyWithRecv)
{
    /* content_length > already: recv_request_body must read from socket. */
    incoming_sender_ctx_t ctx;
    vigil_platform_thread_t *thr = NULL;
    vigil_socket_t sock = connect_to_incoming_sender(&ctx, RECV_REQUEST_BODY_PORT, "abcde", &thr);
    if (sock == VIGIL_INVALID_SOCKET)
        return;

    size_t body_len = 0;
    char *body = recv_request_body(sock, "Content-Length: 5\r\n", NULL, 0, &body_len);
    vigil_platform_tcp_close(sock, NULL);
    vigil_platform_thread_join(thr, NULL);

    EXPECT_NE(body, NULL);
    EXPECT_EQ(body_len, 5u);
    EXPECT_TRUE(body && memcmp(body, "abcde", 5) == 0);
    free(body);
}

/* ── parse_incoming_request: sender closes without double-CRLF ──────  */

#define PARSE_INC_CLOSED_PORT 18805

TEST(VigilHttpTest, ParseIncomingRequestConnectionClosed)
{
    /* Sender closes without \r\n\r\n: recv_request_headers returns NULL. */
    incoming_sender_ctx_t ctx;
    vigil_platform_thread_t *thr = NULL;
    vigil_socket_t sock = connect_to_incoming_sender(&ctx, PARSE_INC_CLOSED_PORT, "GET /path HTTP/1.1\r\n", &thr);
    if (sock == VIGIL_INVALID_SOCKET)
        return;

    http_conn_t conn;
    memset(&conn, 0, sizeof(conn));
    conn.sock = sock;
    int rc = parse_incoming_request(&conn);
    vigil_platform_tcp_close(sock, NULL);
    vigil_platform_thread_join(thr, NULL);

    EXPECT_EQ(rc, -1);
}

/* ── parse_incoming_request: Content-Length exceeds max ─────────────  */

#define PARSE_INC_TOOLARGE_PORT 18806

TEST(VigilHttpTest, ParseIncomingRequestBodyTooLarge)
{
    /* Content-Length value > HTTP_MAX_BODY_BYTES: recv_request_body returns NULL. */
    const char *req = "POST /submit HTTP/1.1\r\n"
                      "Content-Length: 99999999999\r\n"
                      "\r\n";
    incoming_sender_ctx_t ctx;
    vigil_platform_thread_t *thr = NULL;
    vigil_socket_t sock = connect_to_incoming_sender(&ctx, PARSE_INC_TOOLARGE_PORT, req, &thr);
    if (sock == VIGIL_INVALID_SOCKET)
        return;

    http_conn_t conn;
    memset(&conn, 0, sizeof(conn));
    conn.sock = sock;
    int rc = parse_incoming_request(&conn);
    vigil_platform_tcp_close(sock, NULL);
    vigil_platform_thread_join(thr, NULL);

    EXPECT_EQ(rc, -1);
}

/* ── socket_request: response larger than initial buffer ─────────── */

#define LARGE_RESP_PORT 18807

TEST(VigilHttpTest, SocketRequestLargeResponse)
{
    /* Response body > 8192 bytes forces ensure_resp_capacity to grow. */
    const size_t body_len = 9000;
    char *canned = (char *)malloc(100 + body_len + 1);
    if (!canned)
        return;
    int hlen = snprintf(canned, 100, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n", body_len);
    memset(canned + hlen, 'Z', body_len);
    canned[hlen + body_len] = '\0';

    test_server_t srv;
    vigil_platform_thread_t *thr = NULL;
    if (!start_test_server_port(&srv, canned, LARGE_RESP_PORT, &thr))
    {
        free(canned);
        return;
    }

    parsed_url_t url;
    char url_str[64];
    snprintf(url_str, sizeof(url_str), "http://127.0.0.1:%d/big", LARGE_RESP_PORT);
    parse_url(url_str, &url);

    http_response_t resp;
    int rc = socket_request("GET", &url, NULL, NULL, 0, &resp);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(resp.body_len, body_len);
    if (rc == 0)
        response_free(&resp);

    stop_test_server(&srv, thr);
    free(canned);
}

/* ── Test Registration ───────────────────────────────────────────── */

void register_http_tests(void)
{
    /* URL parsing */
    REGISTER_TEST(VigilHttpTest, ParseUrlFull);
    REGISTER_TEST(VigilHttpTest, ParseUrlHttpDefault);
    REGISTER_TEST(VigilHttpTest, ParseUrlHttpsDefaultPort);
    REGISTER_TEST(VigilHttpTest, ParseUrlNoPath);
    REGISTER_TEST(VigilHttpTest, ParseUrlNoScheme);
    REGISTER_TEST(VigilHttpTest, ParseUrlLoopback);
    /* Socket client */
#ifndef __EMSCRIPTEN__
    REGISTER_TEST(VigilHttpTest, SocketGetBasic);
    REGISTER_TEST(VigilHttpTest, SocketPostBody);
    REGISTER_TEST(VigilHttpTest, Socket404Response);
    REGISTER_TEST(VigilHttpTest, SocketEmptyBody);
    REGISTER_TEST(VigilHttpTest, SocketConnectionRefused);
    REGISTER_TEST(VigilHttpTest, SocketRequestLargeHeadersSucceed);
#endif
    /* Cookie jar helpers */
    REGISTER_TEST(VigilHttpTest, CookieJarAppend);
    REGISTER_TEST(VigilHttpTest, CookieJarAppendNoOp);
    REGISTER_TEST(VigilHttpTest, HdrNameMatches);
    REGISTER_TEST(VigilHttpTest, CollectCookiesFromHeaders);
    REGISTER_TEST(VigilHttpTest, CollectCookiesEmpty);
    REGISTER_TEST(VigilHttpTest, BuildRequestHeadersWithCookies);
    REGISTER_TEST(VigilHttpTest, BuildRequestHeadersNoCookies);
    REGISTER_TEST(VigilHttpTest, RedirectChangesMethodTrue);
    REGISTER_TEST(VigilHttpTest, RedirectChangesMethodFalse);
    /* do_request integration */
#ifndef __EMSCRIPTEN__
    REGISTER_TEST(VigilHttpTest, DoRequestLoopback);
    REGISTER_TEST(VigilHttpTest, DoRequestFollowsRedirect);
    REGISTER_TEST(VigilHttpTest, DoRequestHttpsFallbackFails);
#endif
    /* URL parsing extras */
    REGISTER_TEST(VigilHttpTest, ParseUrlUppercaseScheme);
    REGISTER_TEST(VigilHttpTest, ParseUrlPathTooLong);
    REGISTER_TEST(VigilHttpTest, ParseUrlSchemeTooLong);
    REGISTER_TEST(VigilHttpTest, ParseUrlHostTooLong);
    /* Response parsing */
    REGISTER_TEST(VigilHttpTest, ParseHttpResponseValid);
    REGISTER_TEST(VigilHttpTest, ParseHttpResponseNoBody);
    /* Cookie / header helpers */
    REGISTER_TEST(VigilHttpTest, CookieJarAppendTrailingWhitespace);
    REGISTER_TEST(VigilHttpTest, CollectCookiesNoTrailingCrlf);
    REGISTER_TEST(VigilHttpTest, BuildRequestHeadersCookieOnly);
    /* do_request extras */
#ifndef __EMSCRIPTEN__
    REGISTER_TEST(VigilHttpTest, DoRequest307PreservesMethod);
    REGISTER_TEST(VigilHttpTest, DoRequestRedirectLocationTooLong);
#endif
    /* Misc */
    REGISTER_TEST(VigilHttpTest, ResponseFreeNull);
    REGISTER_TEST(VigilHttpTest, ParseHttpResponseMalformed);
    /* Server */
#ifndef __EMSCRIPTEN__
    REGISTER_TEST(VigilHttpTest, ServerRoundTrip);
    REGISTER_TEST(VigilHttpTest, ServerPostRoundTrip);
#endif
    /* Server-side request parsing helpers */
    REGISTER_TEST(VigilHttpTest, ParseRequestLineBasic);
    REGISTER_TEST(VigilHttpTest, ParseRequestLineMissingPath);
    REGISTER_TEST(VigilHttpTest, ParseRequestLineMissingHttpVersion);
    REGISTER_TEST(VigilHttpTest, ParseContentLengthPresent);
    REGISTER_TEST(VigilHttpTest, ParseContentLengthAbsent);
    REGISTER_TEST(VigilHttpTest, ParseContentLengthLowercase);
    REGISTER_TEST(VigilHttpTest, EnsureHdrCapacityNoGrowthNeeded);
    REGISTER_TEST(VigilHttpTest, EnsureHdrCapacityGrows);
    REGISTER_TEST(VigilHttpTest, ParseIncomingRequestGet);
    REGISTER_TEST(VigilHttpTest, ParseIncomingRequestPost);
    REGISTER_TEST(VigilHttpTest, ParseIncomingRequestMalformed);
    /* find_location_header */
    REGISTER_TEST(VigilHttpTest, FindLocationHeaderBasic);
    REGISTER_TEST(VigilHttpTest, FindLocationHeaderLowercase);
    REGISTER_TEST(VigilHttpTest, FindLocationHeaderLeadingSpaces);
    REGISTER_TEST(VigilHttpTest, FindLocationHeaderNull);
    REGISTER_TEST(VigilHttpTest, FindLocationHeaderValueTooLong);
    REGISTER_TEST(VigilHttpTest, FindLocationHeaderNotPresent);
    /* route_matches */
    REGISTER_TEST(VigilHttpTest, RouteMatchesExact);
    REGISTER_TEST(VigilHttpTest, RouteMatchesPrefix);
    REGISTER_TEST(VigilHttpTest, RouteMatchesQueryStripped);
    REGISTER_TEST(VigilHttpTest, RouteMatchesNull);
    /* collect_cookies extras */
    REGISTER_TEST(VigilHttpTest, CollectCookiesWithAttributes);
    /* do_request extras */
#ifndef __EMSCRIPTEN__
    REGISTER_TEST(VigilHttpTest, DoRequest304NotRedirected);
#endif
    /* parse_content_length / recv_body_bytes / recv_request_body coverage */
    REGISTER_TEST(VigilHttpTest, ParseContentLengthTooLarge);
#ifndef __EMSCRIPTEN__
    REGISTER_TEST(VigilHttpTest, RecvBodyBytesPreBuffered);
    REGISTER_TEST(VigilHttpTest, RecvBodyBytesFromSocket);
    REGISTER_TEST(VigilHttpTest, RecvRequestBodyWithRecv);
    REGISTER_TEST(VigilHttpTest, ParseIncomingRequestConnectionClosed);
    REGISTER_TEST(VigilHttpTest, ParseIncomingRequestBodyTooLarge);
    REGISTER_TEST(VigilHttpTest, SocketRequestLargeResponse);
#endif
#if defined(VIGIL_ENABLE_BEARSSL_TLS) && defined(VIGIL_TLS_TEST_CERT_AVAILABLE)
    /* BearSSL TLS */
    REGISTER_TEST(VigilHttpTest, BearSslHttpsGet);
    REGISTER_TEST(VigilHttpTest, BearSslHttpsPost);
#endif
}
