#ifndef VIGIL_PLATFORM_H
#define VIGIL_PLATFORM_H

/*
 * Platform abstraction layer for file I/O.
 *
 * This is an internal header — not part of the public API.
 * Stdlib modules and the CLI call these functions.
 * Each platform provides its own implementation:
 *   platform_posix.c  — Linux, macOS, BSD, Android, iOS
 *   platform_win32.c  — Windows
 *   platform_stub.c   — Emscripten, unknown
 *
 * All allocating functions use the provided allocator.  Pass NULL
 * to use malloc/free.
 */

#include <stddef.h>

#include "vigil/allocator.h"
#include "vigil/export.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** Read an entire file into a newly allocated buffer (null-terminated). */
    VIGIL_API vigil_status_t vigil_platform_read_file(const vigil_allocator_t *allocator, const char *path,
                                                      char **out_data, size_t *out_length, vigil_error_t *error);

    /** Write data to a file, creating or truncating it. */
    VIGIL_API vigil_status_t vigil_platform_write_file(const char *path, const void *data, size_t length,
                                                       vigil_error_t *error);

    /** Check whether a path exists (file or directory). */
    VIGIL_API vigil_status_t vigil_platform_file_exists(const char *path, int *out_exists);

    /** Check whether a path is a directory. */
    VIGIL_API vigil_status_t vigil_platform_is_directory(const char *path, int *out_is_dir);

    /** Create a single directory.  Fails if parent doesn't exist. */
    VIGIL_API vigil_status_t vigil_platform_mkdir(const char *path, vigil_error_t *error);

    /** Create a directory and all missing parents. */
    VIGIL_API vigil_status_t vigil_platform_mkdir_p(const char *path, vigil_error_t *error);

    /** Remove a file or empty directory. */
    VIGIL_API vigil_status_t vigil_platform_remove(const char *path, vigil_error_t *error);

    /** Join two path segments with the platform separator.  Always uses '/'. */
    VIGIL_API vigil_status_t vigil_platform_path_join(const char *base, const char *child, char *out_buf,
                                                      size_t buf_size, vigil_error_t *error);

    /**
     * Read a line from stdin.  Strips trailing newline.
     * If prompt is non-NULL, prints it to stdout first.
     * Returns VIGIL_STATUS_INTERNAL on EOF.
     */
    VIGIL_API vigil_status_t vigil_platform_readline(const char *prompt, char *out_buf, size_t buf_size,
                                                     vigil_error_t *error);

    /*
     * Get the path to the currently running executable.
     */
    VIGIL_API vigil_status_t vigil_platform_self_exe(char *out_buf, size_t buf_size, vigil_error_t *error);

    /* Returns peak resident set size in kilobytes, or 0 on failure. */
    VIGIL_API int64_t vigil_platform_peak_rss_kb(void);

    /*
     * Make a file executable (no-op on Windows).
     */
    VIGIL_API vigil_status_t vigil_platform_make_executable(const char *path, vigil_error_t *error);

    /*
     * List files in a directory (non-recursive).
     * Callback is called for each entry with its name and whether it's a directory.
     * Return VIGIL_STATUS_OK from callback to continue, anything else to stop.
     */
    typedef vigil_status_t (*vigil_platform_dir_callback_t)(const char *name, int is_dir, void *user_data);

    VIGIL_API vigil_status_t vigil_platform_list_dir(const char *path, vigil_platform_dir_callback_t callback,
                                                     void *user_data, vigil_error_t *error);

    /* ── Environment variables ───────────────────────────────────────── */

    /**
     * Get an environment variable.  Returns VIGIL_STATUS_OK and sets
     * *out_value to the value (caller must free with the matching
     * allocator) and *out_found to 1 if found, or *out_found to 0 if
     * not found.
     */
    VIGIL_API vigil_status_t vigil_platform_getenv(const vigil_allocator_t *allocator, const char *name,
                                                   char **out_value, int *out_found, vigil_error_t *error);

    /**
     * Set an environment variable.  Uses setenv on POSIX, _putenv_s on
     * Windows.  Returns VIGIL_STATUS_UNSUPPORTED on stub.
     */
    VIGIL_API vigil_status_t vigil_platform_setenv(const char *name, const char *value, vigil_error_t *error);

    /* ── OS information ──────────────────────────────────────────────── */

    /** Return a static string identifying the OS: "linux", "darwin", "windows", etc. */
    VIGIL_API const char *vigil_platform_os_name(void);

    /** Get the current working directory.  Caller must free *out_path. */
    VIGIL_API vigil_status_t vigil_platform_getcwd(const vigil_allocator_t *allocator, char **out_path,
                                                   vigil_error_t *error);

    /** Get the system temporary directory path.  Caller must free *out_path. */
    VIGIL_API vigil_status_t vigil_platform_temp_dir(const vigil_allocator_t *allocator, char **out_path,
                                                     vigil_error_t *error);

    /** Get the machine hostname.  Caller must free *out_name. */
    VIGIL_API vigil_status_t vigil_platform_hostname(const vigil_allocator_t *allocator, char **out_name,
                                                     vigil_error_t *error);

    /* ── Process execution ───────────────────────────────────────────── */

    /**
     * Execute a child process and capture its output.
     *
     * argv is a NULL-terminated array of strings (argv[0] = program).
     * On success, *out_stdout and *out_stderr are allocated strings the
     * caller must free, and *out_exit_code is the child's exit status.
     * Returns VIGIL_STATUS_UNSUPPORTED on stub/embedded platforms.
     */
    VIGIL_API vigil_status_t vigil_platform_exec(const vigil_allocator_t *allocator, const char *const *argv,
                                                 char **out_stdout, char **out_stderr, int *out_exit_code,
                                                 vigil_error_t *error);

    /* ── Dynamic library loading ─────────────────────────────────────── */

    /**
     * Load a shared library.  Returns an opaque handle.
     * Returns VIGIL_STATUS_UNSUPPORTED on stub/sandboxed platforms.
     */
    VIGIL_API vigil_status_t vigil_platform_dlopen(const char *path, void **out_handle, vigil_error_t *error);

    /**
     * Look up a symbol in a loaded library.  Returns a function pointer.
     */
    VIGIL_API vigil_status_t vigil_platform_dlsym(void *handle, const char *name, void **out_sym, vigil_error_t *error);

    /**
     * Close a loaded library.
     */
    VIGIL_API vigil_status_t vigil_platform_dlclose(void *handle, vigil_error_t *error);

    /* ── Terminal raw mode and key reading (for line editor) ──────────── */

    /** Opaque terminal state saved before entering raw mode. */
    typedef struct vigil_terminal_state vigil_terminal_state_t;

    /** Return 1 if file descriptor 0 (stdin) is a terminal. */
    VIGIL_API int vigil_platform_is_terminal(void);

    /** Enter raw mode.  Caller must call vigil_platform_terminal_restore. */
    VIGIL_API vigil_status_t vigil_platform_terminal_raw(vigil_terminal_state_t **out_state, vigil_error_t *error);

    /** Restore terminal to the state saved by vigil_platform_terminal_raw. */
    VIGIL_API void vigil_platform_terminal_restore(vigil_terminal_state_t *state);

    /** Read a single byte from stdin (blocking). Returns -1 on EOF. */
    VIGIL_API int vigil_platform_terminal_read_byte(void);

    /** Get terminal width in columns.  Returns 80 on failure. */
    VIGIL_API int vigil_platform_terminal_width(void);

    /* ── Portable line editor (implemented in line_editor.c) ─────────── */

    /** History buffer. */
    typedef struct vigil_line_history
    {
        char **entries;
        size_t count;
        size_t capacity;
        size_t max_entries;
    } vigil_line_history_t;

    VIGIL_API void vigil_line_history_init(vigil_line_history_t *h, size_t max_entries);
    VIGIL_API void vigil_line_history_free(vigil_line_history_t *h);
    VIGIL_API void vigil_line_history_add(vigil_line_history_t *h, const char *line);
    VIGIL_API const char *vigil_line_history_get(const vigil_line_history_t *h, size_t index);
    VIGIL_API void vigil_line_history_clear(vigil_line_history_t *h);
    VIGIL_API vigil_status_t vigil_line_history_load(vigil_line_history_t *h, const char *path, vigil_error_t *error);
    VIGIL_API vigil_status_t vigil_line_history_save(const vigil_line_history_t *h, const char *path,
                                                     vigil_error_t *error);

    /**
     * Read a line with editing and history support.
     * Uses raw mode when stdin is a terminal, falls back to fgets otherwise.
     * Returns VIGIL_STATUS_INTERNAL on EOF.
     */
    VIGIL_API vigil_status_t vigil_line_editor_readline(const char *prompt, char *out_buf, size_t buf_size,
                                                        vigil_line_history_t *history, vigil_error_t *error);

    /* ── Extended filesystem operations ──────────────────────────────── */

    /** Copy a file from src to dst. */
    VIGIL_API vigil_status_t vigil_platform_copy_file(const char *src, const char *dst, vigil_error_t *error);

    /** Move/rename a file or directory. */
    VIGIL_API vigil_status_t vigil_platform_rename(const char *src, const char *dst, vigil_error_t *error);

    /** Get file size in bytes. Returns -1 on error. */
    VIGIL_API int64_t vigil_platform_file_size(const char *path);

    /** Get file modification time as Unix timestamp. Returns -1 on error. */
    VIGIL_API int64_t vigil_platform_file_mtime(const char *path);

    /** Check if path is a regular file (not directory/symlink). */
    VIGIL_API vigil_status_t vigil_platform_is_file(const char *path, int *out_is_file);

    /** Create a symbolic link. */
    VIGIL_API vigil_status_t vigil_platform_symlink(const char *target, const char *linkpath, vigil_error_t *error);

    /** Create a hard link. */
    VIGIL_API vigil_status_t vigil_platform_hardlink(const char *target, const char *linkpath, vigil_error_t *error);

    /** Read the target of a symbolic link. Caller must free *out_target. */
    VIGIL_API vigil_status_t vigil_platform_readlink(const vigil_allocator_t *allocator, const char *path,
                                                     char **out_target, vigil_error_t *error);

    /** Check whether path is a symbolic link. */
    VIGIL_API vigil_status_t vigil_platform_is_symlink(const char *path, int *out_is_symlink);

    /** Remove a file, directory, or directory tree recursively. */
    VIGIL_API vigil_status_t vigil_platform_remove_all(const char *path, vigil_error_t *error);

    /** Match a filename against a glob pattern (*, ?). */
    VIGIL_API int vigil_platform_glob_match(const char *pattern, const char *name);

    /** Get user home directory. Caller must free *out_path. */
    VIGIL_API vigil_status_t vigil_platform_home_dir(const vigil_allocator_t *allocator, char **out_path,
                                                     vigil_error_t *error);

    /** Get user config directory (XDG_CONFIG_HOME / ~/Library/Application Support / APPDATA). */
    VIGIL_API vigil_status_t vigil_platform_config_dir(const vigil_allocator_t *allocator, char **out_path,
                                                       vigil_error_t *error);

    /** Get user cache directory (XDG_CACHE_HOME / ~/Library/Caches / LOCALAPPDATA). */
    VIGIL_API vigil_status_t vigil_platform_cache_dir(const vigil_allocator_t *allocator, char **out_path,
                                                      vigil_error_t *error);

    /** Get user data directory (XDG_DATA_HOME / ~/Library/Application Support / APPDATA). */
    VIGIL_API vigil_status_t vigil_platform_data_dir(const vigil_allocator_t *allocator, char **out_path,
                                                     vigil_error_t *error);

    /** Create a unique temporary file. Returns path in out_path (caller must free). */
    VIGIL_API vigil_status_t vigil_platform_temp_file(const vigil_allocator_t *allocator, const char *prefix,
                                                      char **out_path, vigil_error_t *error);

    /** Append data to a file. */
    VIGIL_API vigil_status_t vigil_platform_append_file(const char *path, const void *data, size_t length,
                                                        vigil_error_t *error);

    /* ── Threading primitives ────────────────────────────────────────── */

    /** Opaque thread handle. */
    typedef struct vigil_platform_thread vigil_platform_thread_t;

    /** Opaque mutex handle. */
    typedef struct vigil_platform_mutex vigil_platform_mutex_t;

    /** Opaque condition variable handle. */
    typedef struct vigil_platform_cond vigil_platform_cond_t;

    /** Opaque read-write lock handle. */
    typedef struct vigil_platform_rwlock vigil_platform_rwlock_t;

    /** Thread entry function type. */
    typedef void (*vigil_thread_func_t)(void *arg);

    /** Create and start a new thread. Caller must free with thread_join or thread_detach. */
    VIGIL_API vigil_status_t vigil_platform_thread_create(vigil_platform_thread_t **out_thread,
                                                          vigil_thread_func_t func, void *arg, vigil_error_t *error);

    /** Wait for thread to complete and free resources. */
    VIGIL_API vigil_status_t vigil_platform_thread_join(vigil_platform_thread_t *thread, vigil_error_t *error);

    /** Detach thread (will clean up automatically when done). */
    VIGIL_API vigil_status_t vigil_platform_thread_detach(vigil_platform_thread_t *thread, vigil_error_t *error);

    /** Get current thread ID. */
    VIGIL_API uint64_t vigil_platform_thread_current_id(void);

    /** Yield execution to other threads. */
    VIGIL_API void vigil_platform_thread_yield(void);

    /** Sleep for specified milliseconds. */
    VIGIL_API void vigil_platform_thread_sleep(uint64_t milliseconds);

    /** Wall-clock time in milliseconds since the Unix epoch. */
    VIGIL_API int64_t vigil_platform_now_ms(void);

    /** Wall-clock time in nanoseconds since the Unix epoch. */
    VIGIL_API int64_t vigil_platform_now_ns(void);

    /** Create a mutex. Caller must free with mutex_destroy. */
    VIGIL_API vigil_status_t vigil_platform_mutex_create(vigil_platform_mutex_t **out_mutex, vigil_error_t *error);

    /** Destroy a mutex. */
    VIGIL_API void vigil_platform_mutex_destroy(vigil_platform_mutex_t *mutex);

    /** Lock a mutex (blocking). */
    VIGIL_API void vigil_platform_mutex_lock(vigil_platform_mutex_t *mutex);

    /** Unlock a mutex. */
    VIGIL_API void vigil_platform_mutex_unlock(vigil_platform_mutex_t *mutex);

    /** Try to lock a mutex without blocking. Returns 1 if acquired, 0 if not. */
    VIGIL_API int vigil_platform_mutex_trylock(vigil_platform_mutex_t *mutex);

    /** Create a condition variable. Caller must free with cond_destroy. */
    VIGIL_API vigil_status_t vigil_platform_cond_create(vigil_platform_cond_t **out_cond, vigil_error_t *error);

    /** Destroy a condition variable. */
    VIGIL_API void vigil_platform_cond_destroy(vigil_platform_cond_t *cond);

    /** Wait on condition variable. Mutex must be held; released while waiting. */
    VIGIL_API void vigil_platform_cond_wait(vigil_platform_cond_t *cond, vigil_platform_mutex_t *mutex);

    /** Wait on condition variable with timeout.  Returns 1 if signalled, 0 on timeout. */
    VIGIL_API int vigil_platform_cond_timedwait(vigil_platform_cond_t *cond, vigil_platform_mutex_t *mutex,
                                                uint64_t timeout_ms);

    /** Signal one waiting thread. */
    VIGIL_API void vigil_platform_cond_signal(vigil_platform_cond_t *cond);

    /** Signal all waiting threads. */
    VIGIL_API void vigil_platform_cond_broadcast(vigil_platform_cond_t *cond);

    /** Create a read-write lock. Caller must free with rwlock_destroy. */
    VIGIL_API vigil_status_t vigil_platform_rwlock_create(vigil_platform_rwlock_t **out_rwlock, vigil_error_t *error);

    /** Destroy a read-write lock. */
    VIGIL_API void vigil_platform_rwlock_destroy(vigil_platform_rwlock_t *rwlock);

    /** Acquire read lock (shared, multiple readers allowed). */
    VIGIL_API void vigil_platform_rwlock_rdlock(vigil_platform_rwlock_t *rwlock);

    /** Acquire write lock (exclusive). */
    VIGIL_API void vigil_platform_rwlock_wrlock(vigil_platform_rwlock_t *rwlock);

    /** Release read or write lock. */
    VIGIL_API void vigil_platform_rwlock_unlock(vigil_platform_rwlock_t *rwlock);

    /* ── Thread-local storage ────────────────────────────────────────── */

    /** Opaque TLS key. */
    typedef struct vigil_platform_tls_key vigil_platform_tls_key_t;

    /** Create a TLS key. Optional destructor called when thread exits. */
    VIGIL_API vigil_status_t vigil_platform_tls_create(vigil_platform_tls_key_t **out_key, void (*destructor)(void *),
                                                       vigil_error_t *error);

    /** Destroy a TLS key. */
    VIGIL_API void vigil_platform_tls_destroy(vigil_platform_tls_key_t *key);

    /** Set TLS value for current thread. */
    VIGIL_API void vigil_platform_tls_set(vigil_platform_tls_key_t *key, void *value);

    /** Get TLS value for current thread. */
    VIGIL_API void *vigil_platform_tls_get(vigil_platform_tls_key_t *key);

    /* ── TCP sockets ─────────────────────────────────────────────────── */

    /** Opaque socket handle (int64_t, -1 = invalid). */
    typedef int64_t vigil_socket_t;
#define VIGIL_INVALID_SOCKET ((vigil_socket_t) - 1)

    /** Initialise platform networking (e.g. WSAStartup). Safe to call repeatedly. */
    VIGIL_API vigil_status_t vigil_platform_net_init(vigil_error_t *error);

    /** Create a TCP listener bound to host:port. Returns socket handle. */
    VIGIL_API vigil_status_t vigil_platform_tcp_listen(const char *host, int port, vigil_socket_t *out_sock,
                                                       vigil_error_t *error);

    /** Accept a connection on a listening socket. Blocks. */
    VIGIL_API vigil_status_t vigil_platform_tcp_accept(vigil_socket_t listener, vigil_socket_t *out_client,
                                                       vigil_error_t *error);

    /** Connect to host:port. Returns socket handle. */
    VIGIL_API vigil_status_t vigil_platform_tcp_connect(const char *host, int port, vigil_socket_t *out_sock,
                                                        vigil_error_t *error);

    /** Send data on a connected socket. Returns bytes sent. */
    VIGIL_API vigil_status_t vigil_platform_tcp_send(vigil_socket_t sock, const void *data, size_t len,
                                                     size_t *out_sent, vigil_error_t *error);

    /** Receive data from a connected socket. Returns bytes received (0 = EOF). */
    VIGIL_API vigil_status_t vigil_platform_tcp_recv(vigil_socket_t sock, void *buf, size_t cap, size_t *out_received,
                                                     vigil_error_t *error);

    /** Close a socket (listener or connection). */
    VIGIL_API vigil_status_t vigil_platform_tcp_close(vigil_socket_t sock, vigil_error_t *error);

    /** Set read/write timeout on a TCP socket (milliseconds, 0 = no timeout). */
    VIGIL_API vigil_status_t vigil_platform_tcp_set_timeout(vigil_socket_t sock, int timeout_ms, vigil_error_t *error);

    /** Bind a UDP socket to host:port. Returns socket handle. */
    VIGIL_API vigil_status_t vigil_platform_udp_bind(const char *host, int port, vigil_socket_t *out_sock,
                                                     vigil_error_t *error);

    /** Create an unbound UDP socket. Returns socket handle. */
    VIGIL_API vigil_status_t vigil_platform_udp_new(vigil_socket_t *out_sock, vigil_error_t *error);

    /** Send a UDP datagram to host:port. Returns bytes sent. */
    VIGIL_API vigil_status_t vigil_platform_udp_send(vigil_socket_t sock, const char *host, int port, const void *data,
                                                     size_t len, size_t *out_sent, vigil_error_t *error);

    /** Receive a UDP datagram. Returns bytes received (0 on timeout/EOF). */
    VIGIL_API vigil_status_t vigil_platform_udp_recv(vigil_socket_t sock, void *buf, size_t cap, size_t *out_received,
                                                     vigil_error_t *error);

    /* ── HTTP client (native library, runtime-loaded) ────────────────── */

    /** Result of an HTTP request. Caller must free body and headers with free(). */
    typedef struct
    {
        int status_code;
        char *headers;
        size_t headers_len;
        char *body;
        size_t body_len;
    } vigil_http_response_t;

    /**
     * Perform an HTTP/S request using the best available native library
     * (libcurl on POSIX, WinHTTP on Windows), loaded at runtime.
     *
     * Returns VIGIL_STATUS_UNSUPPORTED if no native HTTP library is available.
     * The caller should fall back to a plain-socket HTTP/1.1 path (no TLS).
     */
    VIGIL_API vigil_status_t vigil_platform_http_request(const vigil_allocator_t *allocator, const char *method,
                                                         const char *url,
                                                         const char *headers, /* CRLF-separated, may be NULL */
                                                         const char *body,    /* may be NULL */
                                                         size_t body_len, vigil_http_response_t *out_response,
                                                         vigil_error_t *error);

    /* ── Atomic operations ───────────────────────────────────────────── */

    /** Atomically load a 64-bit value. */
    VIGIL_API int64_t vigil_atomic_load(const volatile int64_t *ptr);

    /** Atomically store a 64-bit value. */
    VIGIL_API void vigil_atomic_store(volatile int64_t *ptr, int64_t value);

    /** Atomically add to a 64-bit value, return previous value. */
    VIGIL_API int64_t vigil_atomic_add(volatile int64_t *ptr, int64_t value);

    /** Atomically subtract from a 64-bit value, return previous value. */
    VIGIL_API int64_t vigil_atomic_sub(volatile int64_t *ptr, int64_t value);

    /** Atomically compare and swap. Returns 1 if swapped, 0 if not. */
    VIGIL_API int vigil_atomic_cas(volatile int64_t *ptr, int64_t expected, int64_t desired);

    /** Atomically exchange, return previous value. */
    VIGIL_API int64_t vigil_atomic_exchange(volatile int64_t *ptr, int64_t value);

    /** Atomically OR, return previous value. */
    VIGIL_API int64_t vigil_atomic_fetch_or(volatile int64_t *ptr, int64_t value);

    /** Atomically AND, return previous value. */
    VIGIL_API int64_t vigil_atomic_fetch_and(volatile int64_t *ptr, int64_t value);

    /** Atomically XOR, return previous value. */
    VIGIL_API int64_t vigil_atomic_fetch_xor(volatile int64_t *ptr, int64_t value);

    /** Issue a sequentially-consistent memory fence. */
    VIGIL_API void vigil_atomic_fence(void);

    /* ── TLS certificate store ──────────────────────────────────────── */

    /**
     * Callback invoked once per DER-encoded trust anchor certificate.
     * @param der      DER bytes (not NUL-terminated).
     * @param der_len  Length of @der.
     * @param userdata Caller-supplied context pointer.
     * @return         0 to continue enumeration, non-zero to stop early.
     */
    typedef int (*vigil_tls_ca_cb_t)(const unsigned char *der, size_t der_len, void *userdata);

    /**
     * Enumerate system TLS trust-anchor certificates.
     * Calls @cb once for each certificate in the platform certificate store.
     * @return Number of certificates delivered, or -1 if the store is unavailable.
     */
    VIGIL_API int vigil_platform_enumerate_tls_cas(vigil_tls_ca_cb_t cb, void *userdata);

#ifdef __cplusplus
}
#endif

#endif
