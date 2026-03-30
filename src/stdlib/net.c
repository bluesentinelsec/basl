/* VIGIL standard library: net module. */

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/runtime.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_nanbox.h"
#include "platform/platform.h"

/* ── Socket storage ──────────────────────────────────────────────── */

#define MAX_SOCKETS 256
static vigil_socket_t g_sockets[MAX_SOCKETS];
static int g_socket_types[MAX_SOCKETS]; /* 0=unused, 1=tcp, 2=udp */
static int g_initialized = 0;

static int net_init(vigil_error_t *error)
{
    if (g_initialized)
        return 1;
    if (vigil_platform_net_init(error) != VIGIL_STATUS_OK)
        return 0;
    for (int i = 0; i < MAX_SOCKETS; i++)
    {
        g_sockets[i] = VIGIL_INVALID_SOCKET;
        g_socket_types[i] = 0;
    }
    g_initialized = 1;
    return 1;
}

static int64_t alloc_socket(vigil_socket_t s, int type)
{
    for (int i = 0; i < MAX_SOCKETS; i++)
    {
        if (g_socket_types[i] == 0)
        {
            g_sockets[i] = s;
            g_socket_types[i] = type;
            return i;
        }
    }
    return -1;
}

static vigil_socket_t get_socket(int64_t handle)
{
    if (handle < 0 || handle >= MAX_SOCKETS)
        return VIGIL_INVALID_SOCKET;
    if (g_socket_types[handle] == 0)
        return VIGIL_INVALID_SOCKET;
    return g_sockets[handle];
}

static void free_socket(int64_t handle)
{
    if (handle < 0 || handle >= MAX_SOCKETS)
        return;
    if (g_socket_types[handle] != 0)
    {
        vigil_platform_tcp_close(g_sockets[handle], NULL);
        g_sockets[handle] = VIGIL_INVALID_SOCKET;
        g_socket_types[handle] = 0;
    }
}

/* ── Helpers ─────────────────────────────────────────────────────── */

static bool get_string_arg(vigil_vm_t *vm, size_t base, size_t idx, const char **out, size_t *out_len)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    if (!vigil_nanbox_is_object(v))
        return false;
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(v);
    if (!obj || vigil_object_type(obj) != VIGIL_OBJECT_STRING)
        return false;
    *out = vigil_string_object_c_str(obj);
    *out_len = vigil_string_object_length(obj);
    return true;
}

static int64_t get_i64_arg(vigil_vm_t *vm, size_t base, size_t idx)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    if (vigil_nanbox_is_int(v))
        return vigil_nanbox_decode_int(v);
    return 0;
}

static int32_t get_i32_arg(vigil_vm_t *vm, size_t base, size_t idx)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    return vigil_nanbox_decode_i32(v);
}

static vigil_status_t push_i64(vigil_vm_t *vm, int64_t val, vigil_error_t *error)
{
    vigil_value_t v = vigil_nanbox_encode_int(val);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t push_i32(vigil_vm_t *vm, int32_t val, vigil_error_t *error)
{
    vigil_value_t v = vigil_nanbox_encode_i32(val);
    return vigil_vm_stack_push(vm, &v, error);
}

static int32_t clamp_i32(size_t value)
{
    if (value > (size_t)INT32_MAX)
        return INT32_MAX;
    return (int32_t)value;
}

static vigil_status_t push_string(vigil_vm_t *vm, const char *str, size_t len, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_value_t v;
    vigil_status_t s = vigil_string_object_new(vigil_vm_runtime(vm), str, len, &obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_init_object(&v, &obj);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

/* ── TCP Functions ───────────────────────────────────────────────── */

/* net.tcp_listen(host: string, port: i32) -> i64 */
static vigil_status_t net_tcp_listen(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *host;
    size_t host_len;
    int32_t port;
    char host_buf[256];
    vigil_socket_t sock = VIGIL_INVALID_SOCKET;

    if (!net_init(NULL))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }

    if (!get_string_arg(vm, base, 0, &host, &host_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    port = get_i32_arg(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (host_len >= sizeof(host_buf))
        host_len = sizeof(host_buf) - 1;
    memcpy(host_buf, host, host_len);
    host_buf[host_len] = '\0';

    if (vigil_platform_tcp_listen(host_buf, (int)port, &sock, NULL) != VIGIL_STATUS_OK)
    {
        return push_i64(vm, -1, error);
    }

    int64_t handle = alloc_socket(sock, 1);
    if (handle < 0)
    {
        vigil_platform_tcp_close(sock, NULL);
        return push_i64(vm, -1, error);
    }

    return push_i64(vm, handle, error);
}

/* net.tcp_accept(listener: i64) -> i64 */
static vigil_status_t net_tcp_accept(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t listener = get_i64_arg(vm, base, 0);
    vigil_socket_t client = VIGIL_INVALID_SOCKET;
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_socket_t listen_sock = get_socket(listener);
    if (listen_sock == VIGIL_INVALID_SOCKET)
        return push_i64(vm, -1, error);
    if (vigil_platform_tcp_accept(listen_sock, &client, NULL) != VIGIL_STATUS_OK)
    {
        return push_i64(vm, -1, error);
    }

    int64_t handle = alloc_socket(client, 1);
    if (handle < 0)
    {
        vigil_platform_tcp_close(client, NULL);
        return push_i64(vm, -1, error);
    }

    return push_i64(vm, handle, error);
}

/* net.tcp_connect(host: string, port: i32) -> i64 */
static vigil_status_t net_tcp_connect(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *host;
    size_t host_len;
    int32_t port;
    char host_buf[256];
    vigil_socket_t sock = VIGIL_INVALID_SOCKET;

    if (!net_init(NULL))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }

    if (!get_string_arg(vm, base, 0, &host, &host_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    port = get_i32_arg(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (host_len >= sizeof(host_buf))
        host_len = sizeof(host_buf) - 1;
    memcpy(host_buf, host, host_len);
    host_buf[host_len] = '\0';

    if (vigil_platform_tcp_connect(host_buf, (int)port, &sock, NULL) != VIGIL_STATUS_OK)
    {
        return push_i64(vm, -1, error);
    }

    int64_t handle = alloc_socket(sock, 1);
    if (handle < 0)
    {
        vigil_platform_tcp_close(sock, NULL);
        return push_i64(vm, -1, error);
    }

    return push_i64(vm, handle, error);
}

/* net.read(sock: i64, max_bytes: i32) -> string */
static vigil_status_t net_read(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int32_t max_bytes = get_i32_arg(vm, base, 1);
    vigil_runtime_t *runtime = vigil_vm_runtime(vm);
    void *memory = NULL;
    size_t received = 0;
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_socket_t sock = get_socket(handle);
    if (sock == VIGIL_INVALID_SOCKET)
        return push_string(vm, "", 0, error);

    if (max_bytes <= 0)
        max_bytes = 4096;
    if (max_bytes > 1024 * 1024)
        max_bytes = 1024 * 1024;

    if (vigil_runtime_alloc(runtime, (size_t)max_bytes, &memory, error) != VIGIL_STATUS_OK)
    {
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    if (vigil_platform_tcp_recv(sock, memory, (size_t)max_bytes, &received, NULL) != VIGIL_STATUS_OK || received == 0U)
    {
        vigil_runtime_free(runtime, &memory);
        return push_string(vm, "", 0, error);
    }

    vigil_status_t s = push_string(vm, (const char *)memory, received, error);
    vigil_runtime_free(runtime, &memory);
    return s;
}

/* net.write(sock: i64, data: string) -> i32 */
static vigil_status_t net_write(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    const char *data;
    size_t data_len;

    if (!get_string_arg(vm, base, 1, &data, &data_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i32(vm, -1, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_socket_t sock = get_socket(handle);
    size_t sent = 0U;
    if (sock == VIGIL_INVALID_SOCKET)
        return push_i32(vm, -1, error);
    if (vigil_platform_tcp_send(sock, data, data_len, &sent, NULL) != VIGIL_STATUS_OK)
    {
        return push_i32(vm, -1, error);
    }
    return push_i32(vm, clamp_i32(sent), error);
}

/* net.close(sock: i64) */
static vigil_status_t net_close(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    free_socket(handle);
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── UDP Functions ───────────────────────────────────────────────── */

/* net.udp_bind(host: string, port: i32) -> i64 */
static vigil_status_t net_udp_bind(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *host;
    size_t host_len;
    int32_t port;
    char host_buf[256];
    vigil_socket_t sock = VIGIL_INVALID_SOCKET;

    if (!net_init(NULL))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }

    if (!get_string_arg(vm, base, 0, &host, &host_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    port = get_i32_arg(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (host_len >= sizeof(host_buf))
        host_len = sizeof(host_buf) - 1;
    memcpy(host_buf, host, host_len);
    host_buf[host_len] = '\0';

    if (vigil_platform_udp_bind(host_buf, (int)port, &sock, NULL) != VIGIL_STATUS_OK)
    {
        return push_i64(vm, -1, error);
    }

    int64_t handle = alloc_socket(sock, 2);
    if (handle < 0)
    {
        vigil_platform_tcp_close(sock, NULL);
        return push_i64(vm, -1, error);
    }

    return push_i64(vm, handle, error);
}

/* net.udp_new() -> i64 */
static vigil_status_t net_udp_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    vigil_socket_t sock = VIGIL_INVALID_SOCKET;
    if (!net_init(NULL))
        return push_i64(vm, -1, error);
    if (vigil_platform_udp_new(&sock, NULL) != VIGIL_STATUS_OK)
        return push_i64(vm, -1, error);

    int64_t handle = alloc_socket(sock, 2);
    if (handle < 0)
    {
        vigil_platform_tcp_close(sock, NULL);
        return push_i64(vm, -1, error);
    }

    return push_i64(vm, handle, error);
}

/* net.udp_send(sock: i64, host: string, port: i32, data: string) -> i32 */
static vigil_status_t net_udp_send(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    const char *host, *data;
    size_t host_len, data_len;
    int32_t port;
    char host_buf[256];
    size_t sent = 0U;

    if (!get_string_arg(vm, base, 1, &host, &host_len) || !get_string_arg(vm, base, 3, &data, &data_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i32(vm, -1, error);
    }
    port = get_i32_arg(vm, base, 2);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_socket_t sock = get_socket(handle);
    if (sock == VIGIL_INVALID_SOCKET)
        return push_i32(vm, -1, error);

    if (host_len >= sizeof(host_buf))
        host_len = sizeof(host_buf) - 1;
    memcpy(host_buf, host, host_len);
    host_buf[host_len] = '\0';

    if (vigil_platform_udp_send(sock, host_buf, (int)port, data, data_len, &sent, NULL) != VIGIL_STATUS_OK)
    {
        return push_i32(vm, -1, error);
    }
    return push_i32(vm, clamp_i32(sent), error);
}

/* net.udp_recv(sock: i64, max_bytes: i32) -> string */
static vigil_status_t net_udp_recv(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int32_t max_bytes = get_i32_arg(vm, base, 1);
    vigil_runtime_t *runtime = vigil_vm_runtime(vm);
    void *memory = NULL;
    size_t received = 0U;
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_socket_t sock = get_socket(handle);
    if (sock == VIGIL_INVALID_SOCKET)
        return push_string(vm, "", 0, error);

    if (max_bytes <= 0)
        max_bytes = 4096;
    if (max_bytes > 65536)
        max_bytes = 65536;

    if (vigil_runtime_alloc(runtime, (size_t)max_bytes, &memory, error) != VIGIL_STATUS_OK)
    {
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    if (vigil_platform_udp_recv(sock, memory, (size_t)max_bytes, &received, NULL) != VIGIL_STATUS_OK || received == 0U)
    {
        vigil_runtime_free(runtime, &memory);
        return push_string(vm, "", 0, error);
    }

    vigil_status_t s = push_string(vm, (const char *)memory, received, error);
    vigil_runtime_free(runtime, &memory);
    return s;
}

/* net.set_timeout(sock: i64, ms: i32) -> bool */
static vigil_status_t net_set_timeout(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int32_t ms = get_i32_arg(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_socket_t sock = get_socket(handle);
    if (sock == VIGIL_INVALID_SOCKET)
    {
        vigil_value_t v;
        vigil_value_init_bool(&v, 0);
        return vigil_vm_stack_push(vm, &v, error);
    }

    vigil_value_t v;
    vigil_value_init_bool(&v, vigil_platform_tcp_set_timeout(sock, ms, NULL) == VIGIL_STATUS_OK);
    return vigil_vm_stack_push(vm, &v, error);
}

/* ── Module definition ───────────────────────────────────────────── */

static const int str_i32_param[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_I32};
static const int i64_param[] = {VIGIL_TYPE_I64};
static const int i64_i32_param[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32};
static const int i64_str_param[] = {VIGIL_TYPE_I64, VIGIL_TYPE_STRING};
static const int udp_send_param[] = {VIGIL_TYPE_I64, VIGIL_TYPE_STRING, VIGIL_TYPE_I32, VIGIL_TYPE_STRING};

static const vigil_native_module_function_t net_functions[] = {
    {"tcp_listen", 10U, net_tcp_listen, 2U, str_i32_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U},
    {"tcp_accept", 10U, net_tcp_accept, 1U, i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U},
    {"tcp_connect", 11U, net_tcp_connect, 2U, str_i32_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U},
    {"read", 4U, net_read, 2U, i64_i32_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U},
    {"write", 5U, net_write, 2U, i64_str_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U},
    {"close", 5U, net_close, 1U, i64_param, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U},
    {"udp_bind", 8U, net_udp_bind, 2U, str_i32_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U},
    {"udp_new", 7U, net_udp_new, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U},
    {"udp_send", 8U, net_udp_send, 4U, udp_send_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U},
    {"udp_recv", 8U, net_udp_recv, 2U, i64_i32_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U},
    {"set_timeout", 11U, net_set_timeout, 2U, i64_i32_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U},
};

#define NET_FUNCTION_COUNT (sizeof(net_functions) / sizeof(net_functions[0]))

VIGIL_API const vigil_native_module_t vigil_stdlib_net = {"net", 3U, net_functions, NET_FUNCTION_COUNT, NULL, 0U};
