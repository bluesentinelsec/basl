#include "platform.h"

#include <string.h>

#include "internal/vigil_internal.h"

/* Stub implementation — returns VIGIL_STATUS_UNSUPPORTED for all operations. */

vigil_status_t vigil_platform_read_file(const vigil_allocator_t *allocator, const char *path, char **out_data,
                                        size_t *out_length, vigil_error_t *error)
{
    (void)allocator;
    (void)path;
    (void)out_data;
    (void)out_length;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "file I/O is not supported on this platform");
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_write_file(const char *path, const void *data, size_t length, vigil_error_t *error)
{
    (void)path;
    (void)data;
    (void)length;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "file I/O is not supported on this platform");
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_file_exists(const char *path, int *out_exists)
{
    (void)path;
    (void)out_exists;
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_is_directory(const char *path, int *out_is_dir)
{
    (void)path;
    (void)out_is_dir;
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_mkdir(const char *path, vigil_error_t *error)
{
    (void)path;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "file I/O is not supported on this platform");
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_mkdir_p(const char *path, vigil_error_t *error)
{
    (void)path;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "file I/O is not supported on this platform");
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_remove(const char *path, vigil_error_t *error)
{
    (void)path;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "file I/O is not supported on this platform");
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_path_join(const char *base, const char *child, char *out_buf, size_t buf_size,
                                        vigil_error_t *error)
{
    (void)base;
    (void)child;
    (void)out_buf;
    (void)buf_size;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "file I/O is not supported on this platform");
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_readline(const char *prompt, char *out_buf, size_t buf_size, vigil_error_t *error)
{
    (void)prompt;
    (void)out_buf;
    (void)buf_size;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "stdin is not supported on this platform");
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_self_exe(char *out_buf, size_t buf_size, vigil_error_t *error)
{
    (void)out_buf;
    (void)buf_size;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "not supported";
        error->length = 13;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

int64_t vigil_platform_peak_rss_kb(void)
{
    return 0;
}

vigil_status_t vigil_platform_make_executable(const char *path, vigil_error_t *error)
{
    (void)path;
    (void)error;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_platform_list_dir(const char *path, vigil_platform_dir_callback_t callback, void *user_data,
                                       vigil_error_t *error)
{
    (void)path;
    (void)callback;
    (void)user_data;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "not supported";
        error->length = 13;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

/* ── Environment variables ───────────────────────────────────────── */

vigil_status_t vigil_platform_getenv(const vigil_allocator_t *allocator, const char *name, char **out_value,
                                     int *out_found, vigil_error_t *error)
{
    (void)allocator;
    (void)name;
    (void)out_value;
    (void)out_found;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "not supported";
        error->length = 13;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_setenv(const char *name, const char *value, vigil_error_t *error)
{
    (void)name;
    (void)value;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "not supported";
        error->length = 13;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

/* ── OS information ──────────────────────────────────────────────── */

const char *vigil_platform_os_name(void)
{
    return "unknown";
}

vigil_status_t vigil_platform_getcwd(const vigil_allocator_t *allocator, char **out_path, vigil_error_t *error)
{
    (void)allocator;
    (void)out_path;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "not supported";
        error->length = 13;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_temp_dir(const vigil_allocator_t *allocator, char **out_path, vigil_error_t *error)
{
    (void)allocator;
    (void)out_path;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "not supported";
        error->length = 13;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_hostname(const vigil_allocator_t *allocator, char **out_name, vigil_error_t *error)
{
    (void)allocator;
    (void)out_name;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "not supported";
        error->length = 13;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

/* ── Environment variables (new) ──────────────────────────────────── */

vigil_status_t vigil_platform_unsetenv(const char *key, vigil_error_t *error)
{
    (void)key;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "not supported");
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_environ(char ***out_env, size_t *out_count, vigil_error_t *error)
{
    (void)out_env;
    (void)out_count;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "not supported");
    return VIGIL_STATUS_UNSUPPORTED;
}

/* ── Process execution ───────────────────────────────────────────── */

vigil_status_t vigil_platform_exec(const vigil_allocator_t *allocator, const char *const *argv, char **out_stdout,
                                   char **out_stderr, int *out_exit_code, vigil_error_t *error)
{
    (void)allocator;
    (void)argv;
    (void)out_stdout;
    (void)out_stderr;
    (void)out_exit_code;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "not supported";
        error->length = 13;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_exec_streaming(const char *const *argv, int *out_exit_code, vigil_error_t *error)
{
    (void)argv;
    (void)out_exit_code;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "not supported");
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_process_start(const char *const *argv, vigil_process_t **out_process,
                                            vigil_error_t *error)
{
    (void)argv;
    (void)out_process;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "not supported");
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_process_wait(vigil_process_t **process, int *out_exit_code, vigil_error_t *error)
{
    (void)process;
    (void)out_exit_code;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "not supported");
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_process_kill(vigil_process_t **process, vigil_error_t *error)
{
    (void)process;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "not supported");
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_dlopen(const char *path, void **out_handle, vigil_error_t *error)
{
    (void)path;
    (void)out_handle;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "not supported";
        error->length = 13;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_dlsym(void *handle, const char *name, void **out_sym, vigil_error_t *error)
{
    (void)handle;
    (void)name;
    (void)out_sym;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "not supported";
        error->length = 13;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_dlclose(void *handle, vigil_error_t *error)
{
    (void)handle;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "not supported";
        error->length = 13;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

/* ── Terminal raw mode (stub — no terminal support) ──────────────── */

int vigil_platform_is_terminal(void)
{
    return 0;
}

vigil_status_t vigil_platform_terminal_raw(vigil_terminal_state_t **out_state, vigil_error_t *error)
{
    (void)out_state;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "no terminal support");
    return VIGIL_STATUS_UNSUPPORTED;
}

void vigil_platform_terminal_restore(vigil_terminal_state_t *state)
{
    (void)state;
}
int vigil_platform_terminal_read_byte(void)
{
    return -1;
}
int vigil_platform_terminal_width(void)
{
    return 80;
}

/* ── Threading primitives (stub — single-threaded only) ──────────── */

vigil_status_t vigil_platform_thread_create(vigil_platform_thread_t **out_thread, vigil_thread_func_t func, void *arg,
                                            vigil_error_t *error)
{
    (void)out_thread;
    (void)func;
    (void)arg;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "threading not supported");
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_thread_join(vigil_platform_thread_t *thread, vigil_error_t *error)
{
    (void)thread;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "threading not supported");
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_thread_detach(vigil_platform_thread_t *thread, vigil_error_t *error)
{
    (void)thread;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "threading not supported");
    return VIGIL_STATUS_UNSUPPORTED;
}

uint64_t vigil_platform_thread_current_id(void)
{
    return 1;
}
void vigil_platform_thread_yield(void)
{
}
void vigil_platform_thread_sleep(uint64_t milliseconds)
{
    (void)milliseconds;
}
int64_t vigil_platform_now_ms(void)
{
    return 0;
}
int64_t vigil_platform_now_ns(void)
{
    return 0;
}

vigil_status_t vigil_platform_mutex_create(vigil_platform_mutex_t **out_mutex, vigil_error_t *error)
{
    (void)error;
    *out_mutex = (vigil_platform_mutex_t *)1; /* dummy non-NULL */
    return VIGIL_STATUS_OK;
}

void vigil_platform_mutex_destroy(vigil_platform_mutex_t *mutex)
{
    (void)mutex;
}
void vigil_platform_mutex_lock(vigil_platform_mutex_t *mutex)
{
    (void)mutex;
}
void vigil_platform_mutex_unlock(vigil_platform_mutex_t *mutex)
{
    (void)mutex;
}
int vigil_platform_mutex_trylock(vigil_platform_mutex_t *mutex)
{
    (void)mutex;
    return 1;
}

vigil_status_t vigil_platform_cond_create(vigil_platform_cond_t **out_cond, vigil_error_t *error)
{
    (void)error;
    *out_cond = (vigil_platform_cond_t *)1;
    return VIGIL_STATUS_OK;
}

void vigil_platform_cond_destroy(vigil_platform_cond_t *cond)
{
    (void)cond;
}
void vigil_platform_cond_wait(vigil_platform_cond_t *cond, vigil_platform_mutex_t *mutex)
{
    (void)cond;
    (void)mutex;
}
int vigil_platform_cond_timedwait(vigil_platform_cond_t *cond, vigil_platform_mutex_t *mutex, uint64_t timeout_ms)
{
    (void)cond;
    (void)mutex;
    (void)timeout_ms;
    return 0;
}
void vigil_platform_cond_signal(vigil_platform_cond_t *cond)
{
    (void)cond;
}
void vigil_platform_cond_broadcast(vigil_platform_cond_t *cond)
{
    (void)cond;
}

vigil_status_t vigil_platform_rwlock_create(vigil_platform_rwlock_t **out_rwlock, vigil_error_t *error)
{
    (void)error;
    *out_rwlock = (vigil_platform_rwlock_t *)1;
    return VIGIL_STATUS_OK;
}

void vigil_platform_rwlock_destroy(vigil_platform_rwlock_t *rwlock)
{
    (void)rwlock;
}
void vigil_platform_rwlock_rdlock(vigil_platform_rwlock_t *rwlock)
{
    (void)rwlock;
}
void vigil_platform_rwlock_wrlock(vigil_platform_rwlock_t *rwlock)
{
    (void)rwlock;
}
void vigil_platform_rwlock_unlock(vigil_platform_rwlock_t *rwlock)
{
    (void)rwlock;
}

vigil_status_t vigil_platform_tls_create(vigil_platform_tls_key_t **out_key, void (*destructor)(void *),
                                         vigil_error_t *error)
{
    (void)destructor;
    (void)error;
    *out_key = (vigil_platform_tls_key_t *)1;
    return VIGIL_STATUS_OK;
}

void vigil_platform_tls_destroy(vigil_platform_tls_key_t *key)
{
    (void)key;
}
void vigil_platform_tls_set(vigil_platform_tls_key_t *key, void *value)
{
    (void)key;
    (void)value;
}
void *vigil_platform_tls_get(vigil_platform_tls_key_t *key)
{
    (void)key;
    return NULL;
}

/* ── Atomic operations (stub — non-atomic fallback) ──────────────── */

int64_t vigil_atomic_load(const volatile int64_t *ptr)
{
    return *ptr;
}
void vigil_atomic_store(volatile int64_t *ptr, int64_t value)
{
    *ptr = value;
}
int64_t vigil_atomic_add(volatile int64_t *ptr, int64_t value)
{
    int64_t old = *ptr;
    *ptr += value;
    return old;
}
int64_t vigil_atomic_sub(volatile int64_t *ptr, int64_t value)
{
    int64_t old = *ptr;
    *ptr -= value;
    return old;
}
int vigil_atomic_cas(volatile int64_t *ptr, int64_t expected, int64_t desired)
{
    if (*ptr == expected)
    {
        *ptr = desired;
        return 1;
    }
    return 0;
}
int64_t vigil_atomic_exchange(volatile int64_t *ptr, int64_t value)
{
    int64_t old = *ptr;
    *ptr = value;
    return old;
}
int64_t vigil_atomic_fetch_or(volatile int64_t *ptr, int64_t value)
{
    int64_t old = *ptr;
    *ptr |= value;
    return old;
}
int64_t vigil_atomic_fetch_and(volatile int64_t *ptr, int64_t value)
{
    int64_t old = *ptr;
    *ptr &= value;
    return old;
}
int64_t vigil_atomic_fetch_xor(volatile int64_t *ptr, int64_t value)
{
    int64_t old = *ptr;
    *ptr ^= value;
    return old;
}
void vigil_atomic_fence(void)
{
}

/* ── TCP sockets (stub) ──────────────────────────────────────────── */

vigil_status_t vigil_platform_net_init(vigil_error_t *error)
{
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "networking not supported on this platform";
        error->length = 41;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_tcp_listen(const char *host, int port, vigil_socket_t *out_sock, vigil_error_t *error)
{
    (void)host;
    (void)port;
    if (out_sock)
        *out_sock = VIGIL_INVALID_SOCKET;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "networking not supported on this platform";
        error->length = 41;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_tcp_accept(vigil_socket_t listener, vigil_socket_t *out_client, vigil_error_t *error)
{
    (void)listener;
    if (out_client)
        *out_client = VIGIL_INVALID_SOCKET;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "networking not supported on this platform";
        error->length = 41;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_tcp_connect(const char *host, int port, vigil_socket_t *out_sock, vigil_error_t *error)
{
    (void)host;
    (void)port;
    if (out_sock)
        *out_sock = VIGIL_INVALID_SOCKET;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "networking not supported on this platform";
        error->length = 41;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_tcp_send(vigil_socket_t sock, const void *data, size_t len, size_t *out_sent,
                                       vigil_error_t *error)
{
    (void)sock;
    (void)data;
    (void)len;
    if (out_sent)
        *out_sent = 0;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "networking not supported on this platform";
        error->length = 41;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_tcp_recv(vigil_socket_t sock, void *buf, size_t cap, size_t *out_received,
                                       vigil_error_t *error)
{
    (void)sock;
    (void)buf;
    (void)cap;
    if (out_received)
        *out_received = 0;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "networking not supported on this platform";
        error->length = 41;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_tcp_close(vigil_socket_t sock, vigil_error_t *error)
{
    (void)sock;
    (void)error;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_platform_tcp_set_timeout(vigil_socket_t sock, int timeout_ms, vigil_error_t *error)
{
    (void)sock;
    (void)timeout_ms;
    (void)error;
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_udp_bind(const char *host, int port, vigil_socket_t *out_sock, vigil_error_t *error)
{
    (void)host;
    (void)port;
    if (out_sock)
        *out_sock = VIGIL_INVALID_SOCKET;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "networking not supported on this platform";
        error->length = 41;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_udp_new(vigil_socket_t *out_sock, vigil_error_t *error)
{
    if (out_sock)
        *out_sock = VIGIL_INVALID_SOCKET;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "networking not supported on this platform";
        error->length = 41;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_udp_send(vigil_socket_t sock, const char *host, int port, const void *data, size_t len,
                                       size_t *out_sent, vigil_error_t *error)
{
    (void)sock;
    (void)host;
    (void)port;
    (void)data;
    (void)len;
    if (out_sent)
        *out_sent = 0;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "networking not supported on this platform";
        error->length = 41;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_udp_recv(vigil_socket_t sock, void *buf, size_t cap, size_t *out_received,
                                       vigil_error_t *error)
{
    (void)sock;
    (void)buf;
    (void)cap;
    if (out_received)
        *out_received = 0;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "networking not supported on this platform";
        error->length = 41;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

/* ── HTTP client (stub) ──────────────────────────────────────────── */

vigil_status_t vigil_platform_http_request(const vigil_allocator_t *allocator, const char *method, const char *url,
                                           const char *headers, const char *body, size_t body_len,
                                           vigil_http_response_t *out, vigil_error_t *error)
{
    (void)allocator;
    (void)method;
    (void)url;
    (void)headers;
    (void)body;
    (void)body_len;
    if (out)
        memset(out, 0, sizeof(*out));
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "HTTP not supported on this platform";
        error->length = 34;
    }
    return VIGIL_STATUS_UNSUPPORTED;
}

/* ── New fs platform stubs ───────────────────────────────────────── */

vigil_status_t vigil_platform_symlink(const char *target, const char *linkpath, vigil_error_t *error)
{
    (void)target;
    (void)linkpath;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "symlink not supported");
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_hardlink(const char *target, const char *linkpath, vigil_error_t *error)
{
    (void)target;
    (void)linkpath;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "hardlink not supported");
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_readlink(const vigil_allocator_t *allocator, const char *path, char **out_target,
                                       vigil_error_t *error)
{
    (void)allocator;
    (void)path;
    *out_target = NULL;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "readlink not supported");
    return VIGIL_STATUS_UNSUPPORTED;
}

vigil_status_t vigil_platform_is_symlink(const char *path, int *out_is_symlink)
{
    (void)path;
    *out_is_symlink = 0;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_platform_remove_all(const char *path, vigil_error_t *error)
{
    (void)path;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "remove_all not supported");
    return VIGIL_STATUS_UNSUPPORTED;
}

int vigil_platform_glob_match(const char *pattern, const char *name)
{
    while (*pattern && *name)
    {
        if (*pattern == '*')
        {
            pattern++;
            if (*pattern == '\0')
                return 1;
            while (*name)
            {
                if (vigil_platform_glob_match(pattern, name))
                    return 1;
                name++;
            }
            return 0;
        }
        else if (*pattern == '?' || *pattern == *name)
        {
            pattern++;
            name++;
        }
        else
        {
            return 0;
        }
    }
    while (*pattern == '*')
        pattern++;
    return *pattern == '\0' && *name == '\0';
}

/* ── TLS certificate store ──────────────────────────────────────── */

int vigil_platform_enumerate_tls_cas(vigil_tls_ca_cb_t cb, void *userdata)
{
    (void)cb;
    (void)userdata;
    return -1; /* no certificate store on this platform */
}
