#include "platform.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "internal/vigil_internal.h"

/* ── Allocator helpers ───────────────────────────────────────────── */

static vigil_allocator_t resolve_alloc(const vigil_allocator_t *a)
{
    if (a != NULL && vigil_allocator_is_valid(a))
        return *a;
    return vigil_default_allocator();
}

/* ── File I/O ────────────────────────────────────────────────────── */

vigil_status_t vigil_platform_read_file(const vigil_allocator_t *allocator, const char *path, char **out_data,
                                        size_t *out_length, vigil_error_t *error)
{
    FILE *f;
    long size;
    char *buf;
    size_t nread;
    vigil_allocator_t a;

    if (!path || !out_data || !out_length)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "platform: NULL argument");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    *out_data = NULL;
    *out_length = 0;

    f = fopen(path, "rb");
    if (!f)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "platform: cannot open file");
        return VIGIL_STATUS_INTERNAL;
    }

    if (fseek(f, 0L, SEEK_END) != 0)
    {
        fclose(f);
        goto io_err;
    }
    size = ftell(f);
    if (size < 0)
    {
        fclose(f);
        goto io_err;
    }
    if (fseek(f, 0L, SEEK_SET) != 0)
    {
        fclose(f);
        goto io_err;
    }

    a = resolve_alloc(allocator);
    buf = (char *)a.allocate(a.user_data, (size_t)size + 1);
    if (!buf)
    {
        fclose(f);
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "platform: allocation failed");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    nread = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (nread > (size_t)size)
        nread = (size_t)size;
    buf[nread] = '\0';
    *out_data = buf;
    *out_length = nread;
    return VIGIL_STATUS_OK;

io_err:
    vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "platform: file read failed");
    return VIGIL_STATUS_INTERNAL;
}

vigil_status_t vigil_platform_write_file(const char *path, const void *data, size_t length, vigil_error_t *error)
{
    FILE *f;
    if (!path)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "platform: NULL path");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    f = fopen(path, "wb");
    if (!f)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "platform: cannot create file");
        return VIGIL_STATUS_INTERNAL;
    }
    if (length > 0 && data)
    {
        if (fwrite(data, 1, length, f) != length)
        {
            fclose(f);
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "platform: write failed");
            return VIGIL_STATUS_INTERNAL;
        }
    }
    fclose(f);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_platform_file_exists(const char *path, int *out_exists)
{
    struct stat st;
    if (!path || !out_exists)
        return VIGIL_STATUS_INVALID_ARGUMENT;
    *out_exists = (stat(path, &st) == 0) ? 1 : 0;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_platform_is_directory(const char *path, int *out_is_dir)
{
    struct stat st;
    if (!path || !out_is_dir)
        return VIGIL_STATUS_INVALID_ARGUMENT;
    if (stat(path, &st) != 0)
    {
        *out_is_dir = 0;
        return VIGIL_STATUS_OK;
    }
    *out_is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_platform_mkdir(const char *path, vigil_error_t *error)
{
    if (!path)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "platform: NULL path");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (mkdir(path, 0750) != 0 && errno != EEXIST)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "platform: mkdir failed");
        return VIGIL_STATUS_INTERNAL;
    }
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_platform_mkdir_p(const char *path, vigil_error_t *error)
{
    char buf[4096];
    size_t len;
    size_t i;

    if (!path)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "platform: NULL path");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    len = strlen(path);
    if (len == 0 || len >= sizeof(buf))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "platform: path too long");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    memcpy(buf, path, len + 1);

    for (i = 1; i <= len; i++)
    {
        if (buf[i] == '/' || buf[i] == '\0')
        {
            char saved = buf[i];
            buf[i] = '\0';
            if (mkdir(buf, 0750) != 0 && errno != EEXIST)
            {
                vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "platform: mkdir_p failed");
                return VIGIL_STATUS_INTERNAL;
            }
            buf[i] = saved;
        }
    }
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_platform_remove(const char *path, vigil_error_t *error)
{
    if (!path)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "platform: NULL path");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (remove(path) != 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "platform: remove failed");
        return VIGIL_STATUS_INTERNAL;
    }
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_platform_path_join(const char *base, const char *child, char *out_buf, size_t buf_size,
                                        vigil_error_t *error)
{
    size_t blen, clen, total;
    int need_sep;
    if (!base || !child || !out_buf || buf_size == 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "platform: NULL argument");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    blen = strlen(base);
    clen = strlen(child);
    need_sep = (blen > 0 && base[blen - 1] != '/' && base[blen - 1] != '\\') ? 1 : 0;
    total = blen + (size_t)need_sep + clen;
    if (total >= buf_size)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "platform: path too long");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    memcpy(out_buf, base, blen);
    if (need_sep)
        out_buf[blen] = '/';
    memcpy(out_buf + blen + (size_t)need_sep, child, clen);
    out_buf[total] = '\0';
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_platform_readline(const char *prompt, char *out_buf, size_t buf_size, vigil_error_t *error)
{
    size_t len;
    if (!out_buf || buf_size == 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "platform: NULL argument");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (prompt)
    {
        fputs(prompt, stdout);
        fflush(stdout);
    }
    if (!fgets(out_buf, (int)buf_size, stdin))
    {
        out_buf[0] = '\0';
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "platform: EOF on stdin");
        return VIGIL_STATUS_INTERNAL;
    }
    len = strlen(out_buf);
    while (len > 0 && (out_buf[len - 1] == '\n' || out_buf[len - 1] == '\r'))
    {
        out_buf[--len] = '\0';
    }
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_platform_self_exe(char *out_buf, size_t buf_size, vigil_error_t *error)
{
    if (out_buf == NULL || buf_size == 0)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "null buffer";
            error->length = 11;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
#ifdef __APPLE__
    uint32_t size = (uint32_t)buf_size;
    if (_NSGetExecutablePath(out_buf, &size) != 0)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "buffer too small";
            error->length = 16;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    return VIGIL_STATUS_OK;
#else
    ssize_t len = readlink("/proc/self/exe", out_buf, buf_size - 1);
    if (len < 0 || (size_t)len >= buf_size - 1)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "readlink failed";
            error->length = 15;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    out_buf[(size_t)len] = '\0';
    return VIGIL_STATUS_OK;
#endif
}

vigil_status_t vigil_platform_make_executable(const char *path, vigil_error_t *error)
{
    (void)error;
    if (path == NULL)
        return VIGIL_STATUS_INVALID_ARGUMENT;
    chmod(path, 0755);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_platform_list_dir(const char *path, vigil_platform_dir_callback_t callback, void *user_data,
                                       vigil_error_t *error)
{
    DIR *d = opendir(path);
    if (!d)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "failed to open directory";
            error->length = 24;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL)
    {
        if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0' || (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
        {
            continue;
        }
        int is_dir = (ent->d_type == DT_DIR);
        vigil_status_t s = callback(ent->d_name, is_dir, user_data);
        if (s != VIGIL_STATUS_OK)
            break;
    }
    closedir(d);
    return VIGIL_STATUS_OK;
}

/* ── Environment variables ───────────────────────────────────────── */

VIGIL_API vigil_status_t vigil_platform_getenv(const char *name, char **out_value, int *out_found, vigil_error_t *error)
{
    const char *val;
    (void)error;
    if (!name || !out_value || !out_found)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "null argument";
            error->length = 13;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    val = getenv(name);
    if (val)
    {
        *out_value = strdup(val);
        *out_found = 1;
    }
    else
    {
        *out_value = NULL;
        *out_found = 0;
    }
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_setenv(const char *name, const char *value, vigil_error_t *error)
{
    if (!name || !value)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "null argument";
            error->length = 13;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (setenv(name, value, 1) != 0)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "setenv failed";
            error->length = 13;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    return VIGIL_STATUS_OK;
}

/* ── OS information ──────────────────────────────────────────────── */

VIGIL_API const char *vigil_platform_os_name(void)
{
#if defined(__APPLE__)
    return "darwin";
#elif defined(__linux__)
    return "linux";
#elif defined(__FreeBSD__)
    return "freebsd";
#elif defined(__OpenBSD__)
    return "openbsd";
#elif defined(__NetBSD__)
    return "netbsd";
#elif defined(__ANDROID__)
    return "android";
#else
    return "posix";
#endif
}

VIGIL_API vigil_status_t vigil_platform_getcwd(char **out_path, vigil_error_t *error)
{
    char buf[4096];
    if (!out_path)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "null argument";
            error->length = 13;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (!getcwd(buf, sizeof(buf)))
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "getcwd failed";
            error->length = 13;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    *out_path = strdup(buf);
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_temp_dir(char **out_path, vigil_error_t *error)
{
    const char *tmp;
    if (!out_path)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "null argument";
            error->length = 13;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    tmp = getenv("TMPDIR");
    if (!tmp)
        tmp = "/tmp";
    *out_path = strdup(tmp);
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_hostname(char **out_name, vigil_error_t *error)
{
    char buf[256];
    if (!out_name)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "null argument";
            error->length = 13;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (gethostname(buf, sizeof(buf)) != 0)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "gethostname failed";
            error->length = 18;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    buf[sizeof(buf) - 1] = '\0';
    *out_name = strdup(buf);
    return VIGIL_STATUS_OK;
}

/* ── Process execution ───────────────────────────────────────────── */

VIGIL_API vigil_status_t vigil_platform_exec(const char *const *argv, char **out_stdout, char **out_stderr,
                                             int *out_exit_code, vigil_error_t *error)
{
    int stdout_pipe[2], stderr_pipe[2];
    pid_t pid;

    if (!argv || !argv[0] || !out_stdout || !out_stderr || !out_exit_code)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "null argument";
            error->length = 13;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "pipe failed";
            error->length = 11;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    pid = fork();
    if (pid < 0)
    {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "fork failed";
            error->length = 11;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    if (pid == 0)
    {
        /* Child. */
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }

    /* Parent. */
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    {
        /* Read both pipes. Simple sequential read (sufficient for test harness). */
        char *bufs[2] = {NULL, NULL};
        size_t lens[2] = {0, 0};
        size_t caps[2] = {0, 0};
        int fds[2] = {stdout_pipe[0], stderr_pipe[0]};
        int i;
        int status = 0;

        for (i = 0; i < 2; i++)
        {
            char tmp[4096];
            ssize_t n;
            while ((n = read(fds[i], tmp, sizeof(tmp))) > 0)
            {
                if (lens[i] + (size_t)n >= caps[i])
                {
                    caps[i] = (lens[i] + (size_t)n) * 2 + 1;
                    bufs[i] = realloc(bufs[i], caps[i]);
                }
                memcpy(bufs[i] + lens[i], tmp, (size_t)n);
                lens[i] += (size_t)n;
            }
            close(fds[i]);
            if (!bufs[i])
                bufs[i] = calloc(1, 1);
            bufs[i][lens[i]] = '\0';
        }

        waitpid(pid, &status, 0);
        *out_stdout = bufs[0];
        *out_stderr = bufs[1];
        *out_exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    }
    return VIGIL_STATUS_OK;
}

/* ── Dynamic library loading ─────────────────────────────────────── */

#include <dlfcn.h>

VIGIL_API vigil_status_t vigil_platform_dlopen(const char *path, void **out_handle, vigil_error_t *error)
{
    void *h;
    if (!path || !out_handle)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "null argument";
            error->length = 13;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    h = dlopen(path, RTLD_LAZY);
    if (!h)
    {
        const char *msg = dlerror();
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = msg ? msg : "dlopen failed";
            error->length = msg ? strlen(msg) : 13;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    *out_handle = h;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_dlsym(void *handle, const char *name, void **out_sym, vigil_error_t *error)
{
    void *sym;
    if (!handle || !name || !out_sym)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "null argument";
            error->length = 13;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    dlerror(); /* clear */
    sym = dlsym(handle, name);
    if (!sym)
    {
        const char *msg = dlerror();
        if (msg)
        {
            if (error)
            {
                error->type = VIGIL_STATUS_INTERNAL;
                error->value = msg;
                error->length = strlen(msg);
            }
            return VIGIL_STATUS_INTERNAL;
        }
    }
    *out_sym = sym;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_dlclose(void *handle, vigil_error_t *error)
{
    if (!handle)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "null handle";
            error->length = 11;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    dlclose(handle);
    return VIGIL_STATUS_OK;
}

/* ── Terminal raw mode ───────────────────────────────────────────── */

struct vigil_terminal_state
{
    struct termios orig;
};

int vigil_platform_is_terminal(void)
{
    return isatty(STDIN_FILENO);
}

vigil_status_t vigil_platform_terminal_raw(vigil_terminal_state_t **out_state, vigil_error_t *error)
{
    struct termios raw;
    vigil_terminal_state_t *state = malloc(sizeof(*state));
    if (!state)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "out of memory");
        return VIGIL_STATUS_INTERNAL;
    }
    if (tcgetattr(STDIN_FILENO, &state->orig) == -1)
    {
        free(state);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "tcgetattr failed");
        return VIGIL_STATUS_INTERNAL;
    }
    raw = state->orig;
    raw.c_iflag &= ~(unsigned)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(unsigned)(OPOST);
    raw.c_cflag |= (unsigned)(CS8);
    raw.c_lflag &= ~(unsigned)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    {
        free(state);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "tcsetattr failed");
        return VIGIL_STATUS_INTERNAL;
    }
    *out_state = state;
    return VIGIL_STATUS_OK;
}

void vigil_platform_terminal_restore(vigil_terminal_state_t *state)
{
    if (!state)
        return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &state->orig);
    free(state);
}

int vigil_platform_terminal_read_byte(void)
{
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    return (n <= 0) ? -1 : (int)c;
}

int vigil_platform_terminal_width(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
        return 80;
    return ws.ws_col;
}

/* ── Extended filesystem operations ──────────────────────────────── */

VIGIL_API vigil_status_t vigil_platform_copy_file(const char *src, const char *dst, vigil_error_t *error)
{
    FILE *in = fopen(src, "rb");
    if (!in)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "copy: cannot open source");
        return VIGIL_STATUS_INTERNAL;
    }
    FILE *out = fopen(dst, "wb");
    if (!out)
    {
        fclose(in);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "copy: cannot create destination");
        return VIGIL_STATUS_INTERNAL;
    }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    {
        if (fwrite(buf, 1, n, out) != n)
        {
            fclose(in);
            fclose(out);
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "copy: write failed");
            return VIGIL_STATUS_INTERNAL;
        }
    }
    fclose(in);
    fclose(out);
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_rename(const char *src, const char *dst, vigil_error_t *error)
{
    if (rename(src, dst) != 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "rename failed");
        return VIGIL_STATUS_INTERNAL;
    }
    return VIGIL_STATUS_OK;
}

VIGIL_API int64_t vigil_platform_file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    return (int64_t)st.st_size;
}

VIGIL_API int64_t vigil_platform_file_mtime(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    return (int64_t)st.st_mtime;
}

VIGIL_API vigil_status_t vigil_platform_is_file(const char *path, int *out_is_file)
{
    struct stat st;
    if (stat(path, &st) != 0)
    {
        *out_is_file = 0;
        return VIGIL_STATUS_OK;
    }
    *out_is_file = S_ISREG(st.st_mode) ? 1 : 0;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_symlink(const char *target, const char *linkpath, vigil_error_t *error)
{
    if (symlink(target, linkpath) != 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "symlink failed");
        return VIGIL_STATUS_INTERNAL;
    }
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_hardlink(const char *target, const char *linkpath, vigil_error_t *error)
{
    if (link(target, linkpath) != 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "hardlink failed");
        return VIGIL_STATUS_INTERNAL;
    }
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_readlink(const char *path, char **out_target, vigil_error_t *error)
{
    char buf[PATH_MAX];
    ssize_t len = readlink(path, buf, sizeof(buf) - 1);
    if (len < 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "readlink failed");
        return VIGIL_STATUS_INTERNAL;
    }
    buf[len] = '\0';
    *out_target = strdup(buf);
    if (!*out_target)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_is_symlink(const char *path, int *out_is_symlink)
{
    struct stat st;
    if (lstat(path, &st) != 0)
    {
        *out_is_symlink = 0;
        return VIGIL_STATUS_OK;
    }
    *out_is_symlink = S_ISLNK(st.st_mode) ? 1 : 0;
    return VIGIL_STATUS_OK;
}

static vigil_status_t remove_all_impl(const char *path, vigil_error_t *error)
{
    struct stat st;
    if (lstat(path, &st) != 0)
        return VIGIL_STATUS_OK; /* already gone */
    if (!S_ISDIR(st.st_mode))
    {
        if (unlink(path) != 0)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "remove failed");
            return VIGIL_STATUS_INTERNAL;
        }
        return VIGIL_STATUS_OK;
    }
    DIR *d = opendir(path);
    if (!d)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "opendir failed");
        return VIGIL_STATUS_INTERNAL;
    }
    struct dirent *ent;
    char child[PATH_MAX];
    while ((ent = readdir(d)) != NULL)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        vigil_status_t s = remove_all_impl(child, error);
        if (s != VIGIL_STATUS_OK)
        {
            closedir(d);
            return s;
        }
    }
    closedir(d);
    if (rmdir(path) != 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "rmdir failed");
        return VIGIL_STATUS_INTERNAL;
    }
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_remove_all(const char *path, vigil_error_t *error)
{
    return remove_all_impl(path, error);
}

VIGIL_API int vigil_platform_glob_match(const char *pattern, const char *name)
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

VIGIL_API vigil_status_t vigil_platform_home_dir(char **out_path, vigil_error_t *error)
{
    const char *home = getenv("HOME");
    if (!home)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "HOME not set");
        return VIGIL_STATUS_INTERNAL;
    }
    *out_path = strdup(home);
    if (!*out_path)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_config_dir(char **out_path, vigil_error_t *error)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0])
    {
        *out_path = strdup(xdg);
        if (!*out_path)
            return VIGIL_STATUS_OUT_OF_MEMORY;
        return VIGIL_STATUS_OK;
    }
#ifdef __APPLE__
    const char *home = getenv("HOME");
    if (!home)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "HOME not set");
        return VIGIL_STATUS_INTERNAL;
    }
    size_t len = strlen(home) + 32;
    *out_path = malloc(len);
    if (!*out_path)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    snprintf(*out_path, len, "%s/Library/Application Support", home);
#else
    const char *home = getenv("HOME");
    if (!home)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "HOME not set");
        return VIGIL_STATUS_INTERNAL;
    }
    size_t len = strlen(home) + 16;
    *out_path = malloc(len);
    if (!*out_path)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    snprintf(*out_path, len, "%s/.config", home);
#endif
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_cache_dir(char **out_path, vigil_error_t *error)
{
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg && xdg[0])
    {
        *out_path = strdup(xdg);
        if (!*out_path)
            return VIGIL_STATUS_OUT_OF_MEMORY;
        return VIGIL_STATUS_OK;
    }
#ifdef __APPLE__
    const char *home = getenv("HOME");
    if (!home)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "HOME not set");
        return VIGIL_STATUS_INTERNAL;
    }
    size_t len = strlen(home) + 20;
    *out_path = malloc(len);
    if (!*out_path)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    snprintf(*out_path, len, "%s/Library/Caches", home);
#else
    const char *home = getenv("HOME");
    if (!home)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "HOME not set");
        return VIGIL_STATUS_INTERNAL;
    }
    size_t len = strlen(home) + 16;
    *out_path = malloc(len);
    if (!*out_path)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    snprintf(*out_path, len, "%s/.cache", home);
#endif
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_data_dir(char **out_path, vigil_error_t *error)
{
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && xdg[0])
    {
        *out_path = strdup(xdg);
        if (!*out_path)
            return VIGIL_STATUS_OUT_OF_MEMORY;
        return VIGIL_STATUS_OK;
    }
#ifdef __APPLE__
    const char *home = getenv("HOME");
    if (!home)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "HOME not set");
        return VIGIL_STATUS_INTERNAL;
    }
    size_t len = strlen(home) + 32;
    *out_path = malloc(len);
    if (!*out_path)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    snprintf(*out_path, len, "%s/Library/Application Support", home);
#else
    const char *home = getenv("HOME");
    if (!home)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "HOME not set");
        return VIGIL_STATUS_INTERNAL;
    }
    size_t len = strlen(home) + 20;
    *out_path = malloc(len);
    if (!*out_path)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    snprintf(*out_path, len, "%s/.local/share", home);
#endif
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_temp_file(const char *prefix, char **out_path, vigil_error_t *error)
{
    char *tmpdir = NULL;
    vigil_status_t s = vigil_platform_temp_dir(&tmpdir, error);
    if (s != VIGIL_STATUS_OK)
        return s;

    size_t len = strlen(tmpdir) + strlen(prefix ? prefix : "tmp") + 16;
    char *path = malloc(len);
    if (!path)
    {
        free(tmpdir);
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    snprintf(path, len, "%s/%sXXXXXX", tmpdir, prefix ? prefix : "tmp");
    free(tmpdir);

    int fd = mkstemp(path);
    if (fd < 0)
    {
        free(path);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "mkstemp failed");
        return VIGIL_STATUS_INTERNAL;
    }
    close(fd);
    *out_path = path;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_append_file(const char *path, const void *data, size_t length,
                                                    vigil_error_t *error)
{
    FILE *f = fopen(path, "ab");
    if (!f)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "cannot open file for append");
        return VIGIL_STATUS_INTERNAL;
    }
    if (fwrite(data, 1, length, f) != length)
    {
        fclose(f);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "append write failed");
        return VIGIL_STATUS_INTERNAL;
    }
    fclose(f);
    return VIGIL_STATUS_OK;
}

/* ── Threading primitives ────────────────────────────────────────── */

struct vigil_platform_thread
{
    pthread_t handle;
    vigil_thread_func_t func;
    void *arg;
    volatile int detached;
};

static void *thread_wrapper(void *arg)
{
    vigil_platform_thread_t *t = (vigil_platform_thread_t *)arg;
    t->func(t->arg);
    if (t->detached)
        free(t);
    return NULL;
}

VIGIL_API vigil_status_t vigil_platform_thread_create(vigil_platform_thread_t **out_thread, vigil_thread_func_t func,
                                                      void *arg, vigil_error_t *error)
{
    vigil_platform_thread_t *t = malloc(sizeof(*t));
    if (!t)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    t->func = func;
    t->arg = arg;
    t->detached = 0;
    if (pthread_create(&t->handle, NULL, thread_wrapper, t) != 0)
    {
        free(t);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "pthread_create failed");
        return VIGIL_STATUS_INTERNAL;
    }
    *out_thread = t;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_thread_join(vigil_platform_thread_t *thread, vigil_error_t *error)
{
    if (pthread_join(thread->handle, NULL) != 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "pthread_join failed");
        return VIGIL_STATUS_INTERNAL;
    }
    free(thread);
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_thread_detach(vigil_platform_thread_t *thread, vigil_error_t *error)
{
    if (pthread_detach(thread->handle) != 0)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "pthread_detach failed");
        return VIGIL_STATUS_INTERNAL;
    }
    thread->detached = 1; /* wrapper will free after func returns */
    return VIGIL_STATUS_OK;
}

VIGIL_API uint64_t vigil_platform_thread_current_id(void)
{
#ifdef __APPLE__
    uint64_t tid;
    pthread_threadid_np(NULL, &tid);
    return tid;
#else
    return (uint64_t)pthread_self();
#endif
}

VIGIL_API void vigil_platform_thread_yield(void)
{
    sched_yield();
}

VIGIL_API void vigil_platform_thread_sleep(uint64_t milliseconds)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(milliseconds / 1000);
    ts.tv_nsec = (long)((milliseconds % 1000) * 1000000);
    nanosleep(&ts, NULL);
}

VIGIL_API int64_t vigil_platform_now_ms(void)
{
    struct timeval tv;

    if (gettimeofday(&tv, NULL) != 0)
    {
        return 0;
    }
    return (int64_t)tv.tv_sec * 1000LL + (int64_t)(tv.tv_usec / 1000);
}

VIGIL_API int64_t vigil_platform_now_ns(void)
{
    struct timeval tv;

    if (gettimeofday(&tv, NULL) != 0)
    {
        return 0;
    }
    return (int64_t)tv.tv_sec * 1000000000LL + (int64_t)tv.tv_usec * 1000LL;
}

/* ── Mutex ───────────────────────────────────────────────────────── */

struct vigil_platform_mutex
{
    pthread_mutex_t handle;
};

VIGIL_API vigil_status_t vigil_platform_mutex_create(vigil_platform_mutex_t **out_mutex, vigil_error_t *error)
{
    vigil_platform_mutex_t *m = malloc(sizeof(*m));
    if (!m)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    if (pthread_mutex_init(&m->handle, NULL) != 0)
    {
        free(m);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "pthread_mutex_init failed");
        return VIGIL_STATUS_INTERNAL;
    }
    *out_mutex = m;
    return VIGIL_STATUS_OK;
}

VIGIL_API void vigil_platform_mutex_destroy(vigil_platform_mutex_t *mutex)
{
    if (mutex)
    {
        pthread_mutex_destroy(&mutex->handle);
        free(mutex);
    }
}

VIGIL_API void vigil_platform_mutex_lock(vigil_platform_mutex_t *mutex)
{
    pthread_mutex_lock(&mutex->handle);
}

VIGIL_API void vigil_platform_mutex_unlock(vigil_platform_mutex_t *mutex)
{
    pthread_mutex_unlock(&mutex->handle);
}

VIGIL_API int vigil_platform_mutex_trylock(vigil_platform_mutex_t *mutex)
{
    return pthread_mutex_trylock(&mutex->handle) == 0 ? 1 : 0;
}

/* ── Condition variable ──────────────────────────────────────────── */

struct vigil_platform_cond
{
    pthread_cond_t handle;
};

VIGIL_API vigil_status_t vigil_platform_cond_create(vigil_platform_cond_t **out_cond, vigil_error_t *error)
{
    vigil_platform_cond_t *c = malloc(sizeof(*c));
    if (!c)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    if (pthread_cond_init(&c->handle, NULL) != 0)
    {
        free(c);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "pthread_cond_init failed");
        return VIGIL_STATUS_INTERNAL;
    }
    *out_cond = c;
    return VIGIL_STATUS_OK;
}

VIGIL_API void vigil_platform_cond_destroy(vigil_platform_cond_t *cond)
{
    if (cond)
    {
        pthread_cond_destroy(&cond->handle);
        free(cond);
    }
}

VIGIL_API void vigil_platform_cond_wait(vigil_platform_cond_t *cond, vigil_platform_mutex_t *mutex)
{
    pthread_cond_wait(&cond->handle, &mutex->handle);
}

VIGIL_API int vigil_platform_cond_timedwait(vigil_platform_cond_t *cond, vigil_platform_mutex_t *mutex,
                                            uint64_t timeout_ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)(timeout_ms / 1000);
    ts.tv_nsec += (long)((timeout_ms % 1000) * 1000000);
    if (ts.tv_nsec >= 1000000000L)
    {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    int rc = pthread_cond_timedwait(&cond->handle, &mutex->handle, &ts);
    return rc == 0 ? 1 : 0;
}

VIGIL_API void vigil_platform_cond_signal(vigil_platform_cond_t *cond)
{
    pthread_cond_signal(&cond->handle);
}

VIGIL_API void vigil_platform_cond_broadcast(vigil_platform_cond_t *cond)
{
    pthread_cond_broadcast(&cond->handle);
}

/* ── Read-write lock ─────────────────────────────────────────────── */

struct vigil_platform_rwlock
{
    pthread_rwlock_t handle;
};

VIGIL_API vigil_status_t vigil_platform_rwlock_create(vigil_platform_rwlock_t **out_rwlock, vigil_error_t *error)
{
    vigil_platform_rwlock_t *rw = malloc(sizeof(*rw));
    if (!rw)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    if (pthread_rwlock_init(&rw->handle, NULL) != 0)
    {
        free(rw);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "pthread_rwlock_init failed");
        return VIGIL_STATUS_INTERNAL;
    }
    *out_rwlock = rw;
    return VIGIL_STATUS_OK;
}

VIGIL_API void vigil_platform_rwlock_destroy(vigil_platform_rwlock_t *rwlock)
{
    if (rwlock)
    {
        pthread_rwlock_destroy(&rwlock->handle);
        free(rwlock);
    }
}

VIGIL_API void vigil_platform_rwlock_rdlock(vigil_platform_rwlock_t *rwlock)
{
    pthread_rwlock_rdlock(&rwlock->handle);
}

VIGIL_API void vigil_platform_rwlock_wrlock(vigil_platform_rwlock_t *rwlock)
{
    pthread_rwlock_wrlock(&rwlock->handle);
}

VIGIL_API void vigil_platform_rwlock_unlock(vigil_platform_rwlock_t *rwlock)
{
    pthread_rwlock_unlock(&rwlock->handle);
}

/* ── Thread-local storage ────────────────────────────────────────── */

struct vigil_platform_tls_key
{
    pthread_key_t key;
};

VIGIL_API vigil_status_t vigil_platform_tls_create(vigil_platform_tls_key_t **out_key, void (*destructor)(void *),
                                                   vigil_error_t *error)
{
    vigil_platform_tls_key_t *k = malloc(sizeof(*k));
    if (!k)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    if (pthread_key_create(&k->key, destructor) != 0)
    {
        free(k);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "pthread_key_create failed");
        return VIGIL_STATUS_INTERNAL;
    }
    *out_key = k;
    return VIGIL_STATUS_OK;
}

VIGIL_API void vigil_platform_tls_destroy(vigil_platform_tls_key_t *key)
{
    if (key)
    {
        pthread_key_delete(key->key);
        free(key);
    }
}

VIGIL_API void vigil_platform_tls_set(vigil_platform_tls_key_t *key, void *value)
{
    pthread_setspecific(key->key, value);
}

VIGIL_API void *vigil_platform_tls_get(vigil_platform_tls_key_t *key)
{
    return pthread_getspecific(key->key);
}

/* ── Atomic operations ───────────────────────────────────────────── */

VIGIL_API int64_t vigil_atomic_load(const volatile int64_t *ptr)
{
    return atomic_load((const _Atomic int64_t *)ptr);
}

VIGIL_API void vigil_atomic_store(volatile int64_t *ptr, int64_t value)
{
    atomic_store((_Atomic int64_t *)ptr, value);
}

VIGIL_API int64_t vigil_atomic_add(volatile int64_t *ptr, int64_t value)
{
    return atomic_fetch_add((_Atomic int64_t *)ptr, value);
}

VIGIL_API int64_t vigil_atomic_sub(volatile int64_t *ptr, int64_t value)
{
    return atomic_fetch_sub((_Atomic int64_t *)ptr, value);
}

VIGIL_API int vigil_atomic_cas(volatile int64_t *ptr, int64_t expected, int64_t desired)
{
    return atomic_compare_exchange_strong((_Atomic int64_t *)ptr, &expected, desired) ? 1 : 0;
}

VIGIL_API int64_t vigil_atomic_exchange(volatile int64_t *ptr, int64_t value)
{
    return atomic_exchange((_Atomic int64_t *)ptr, value);
}

VIGIL_API int64_t vigil_atomic_fetch_or(volatile int64_t *ptr, int64_t value)
{
    return atomic_fetch_or((_Atomic int64_t *)ptr, value);
}

VIGIL_API int64_t vigil_atomic_fetch_and(volatile int64_t *ptr, int64_t value)
{
    return atomic_fetch_and((_Atomic int64_t *)ptr, value);
}

VIGIL_API int64_t vigil_atomic_fetch_xor(volatile int64_t *ptr, int64_t value)
{
    return atomic_fetch_xor((_Atomic int64_t *)ptr, value);
}

VIGIL_API void vigil_atomic_fence(void)
{
    atomic_thread_fence(memory_order_seq_cst);
}

/* ── TCP sockets ─────────────────────────────────────────────────── */

#include <arpa/inet.h>
#include <dlfcn.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

VIGIL_API vigil_status_t vigil_platform_net_init(vigil_error_t *error)
{
    (void)error;
    return VIGIL_STATUS_OK; /* no-op on POSIX */
}

VIGIL_API vigil_status_t vigil_platform_tcp_listen(const char *host, int port, vigil_socket_t *out_sock,
                                                   vigil_error_t *error)
{
    if (!host || !out_sock)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "null argument";
            error->length = 13;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    *out_sock = VIGIL_INVALID_SOCKET;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "socket() failed";
            error->length = 15;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (strcmp(host, "0.0.0.0") == 0)
    {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else
    {
        inet_pton(AF_INET, host, &addr.sin_addr);
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(fd);
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "bind() failed";
            error->length = 13;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    if (listen(fd, 128) < 0)
    {
        close(fd);
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "listen() failed";
            error->length = 15;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    *out_sock = (vigil_socket_t)fd;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_tcp_accept(vigil_socket_t listener, vigil_socket_t *out_client,
                                                   vigil_error_t *error)
{
    if (!out_client)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "null argument";
            error->length = 13;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    *out_client = VIGIL_INVALID_SOCKET;
    int client = accept((int)listener, NULL, NULL);
    if (client < 0)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "accept() failed";
            error->length = 15;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    *out_client = (vigil_socket_t)client;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_tcp_connect(const char *host, int port, vigil_socket_t *out_sock,
                                                    vigil_error_t *error)
{
    if (!host || !out_sock)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "null argument";
            error->length = 13;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    *out_sock = VIGIL_INVALID_SOCKET;

    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "getaddrinfo() failed";
            error->length = 20;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0)
    {
        freeaddrinfo(res);
        return VIGIL_STATUS_INTERNAL;
    }

    if (connect(fd, res->ai_addr, (int)res->ai_addrlen) < 0)
    {
        close(fd);
        freeaddrinfo(res);
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "connect() failed";
            error->length = 16;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    freeaddrinfo(res);
    *out_sock = (vigil_socket_t)fd;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_tcp_send(vigil_socket_t sock, const void *data, size_t len, size_t *out_sent,
                                                 vigil_error_t *error)
{
    (void)error;
    ssize_t n = send((int)sock, data, len, 0);
    if (n < 0)
    {
        if (out_sent)
            *out_sent = 0;
        return VIGIL_STATUS_INTERNAL;
    }
    if (out_sent)
        *out_sent = (size_t)n;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_tcp_recv(vigil_socket_t sock, void *buf, size_t cap, size_t *out_received,
                                                 vigil_error_t *error)
{
    (void)error;
    ssize_t n = recv((int)sock, buf, cap, 0);
    if (n < 0)
    {
        if (out_received)
            *out_received = 0;
        return VIGIL_STATUS_INTERNAL;
    }
    if (out_received)
        *out_received = (size_t)n;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_tcp_close(vigil_socket_t sock, vigil_error_t *error)
{
    (void)error;
    close((int)sock);
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_tcp_set_timeout(vigil_socket_t sock, int timeout_ms, vigil_error_t *error)
{
    (void)error;
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt((int)sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt((int)sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_udp_bind(const char *host, int port, vigil_socket_t *out_sock,
                                                 vigil_error_t *error)
{
    int fd;
    struct sockaddr_in addr;

    if (!host || !out_sock)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "null argument";
            error->length = 13;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    *out_sock = VIGIL_INVALID_SOCKET;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "socket() failed";
            error->length = 15;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (host[0] == '\0' || strcmp(host, "0.0.0.0") == 0)
    {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else if (inet_pton(AF_INET, host, &addr.sin_addr) != 1)
    {
        close(fd);
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "inet_pton() failed";
            error->length = 18;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(fd);
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "bind() failed";
            error->length = 13;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    *out_sock = (vigil_socket_t)fd;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_udp_new(vigil_socket_t *out_sock, vigil_error_t *error)
{
    int fd;

    if (!out_sock)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "null argument";
            error->length = 13;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    *out_sock = VIGIL_INVALID_SOCKET;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "socket() failed";
            error->length = 15;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    *out_sock = (vigil_socket_t)fd;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_udp_send(vigil_socket_t sock, const char *host, int port, const void *data,
                                                 size_t len, size_t *out_sent, vigil_error_t *error)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    char port_str[16];
    ssize_t sent;

    if (!host || !data)
    {
        if (out_sent)
            *out_sent = 0U;
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "null argument";
            error->length = 13;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || res == NULL)
    {
        if (out_sent)
            *out_sent = 0U;
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "getaddrinfo() failed";
            error->length = 20;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    sent = sendto((int)sock, data, len, 0, res->ai_addr, (socklen_t)res->ai_addrlen);
    freeaddrinfo(res);
    if (sent < 0)
    {
        if (out_sent)
            *out_sent = 0U;
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "sendto() failed";
            error->length = 16;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    if (out_sent)
        *out_sent = (size_t)sent;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_udp_recv(vigil_socket_t sock, void *buf, size_t cap, size_t *out_received,
                                                 vigil_error_t *error)
{
    ssize_t received;

    if (!buf)
    {
        if (out_received)
            *out_received = 0U;
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "null argument";
            error->length = 13;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    received = recvfrom((int)sock, buf, cap, 0, NULL, NULL);
    if (received < 0)
    {
        if (out_received)
            *out_received = 0U;
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "recvfrom() failed";
            error->length = 18;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    if (out_received)
        *out_received = (size_t)received;
    return VIGIL_STATUS_OK;
}

/* ── HTTP client via libcurl (runtime-loaded) ────────────────────── */

typedef void CURL;
typedef int CURLcode;
#define CURLE_OK 0
#define CURLOPT_URL 10002
#define CURLOPT_WRITEFUNCTION 20011
#define CURLOPT_WRITEDATA 10001
#define CURLOPT_HTTPHEADER 10023
#define CURLOPT_POSTFIELDS 10015
#define CURLOPT_POSTFIELDSIZE 60
#define CURLOPT_CUSTOMREQUEST 10036
#define CURLOPT_HEADERFUNCTION 20079
#define CURLOPT_HEADERDATA 10029
#define CURLINFO_RESPONSE_CODE 0x200002
#define CURLOPT_HTTP_VERSION 84
#define CURL_HTTP_VERSION_2TLS 4
#define CURLOPT_ACCEPT_ENCODING 10102
#define CURL_HDR_LIMIT (256U * 1024U)

typedef CURL *(*curl_easy_init_t)(void);
typedef void (*curl_easy_cleanup_t)(CURL *);
typedef CURLcode (*curl_easy_setopt_t)(CURL *, int, ...);
typedef CURLcode (*curl_easy_perform_t)(CURL *);
typedef CURLcode (*curl_easy_getinfo_t)(CURL *, int, ...);
typedef struct curl_slist *(*curl_slist_append_t)(struct curl_slist *, const char *);
typedef void (*curl_slist_free_all_t)(struct curl_slist *);

static void *g_curl_lib = NULL;
static curl_easy_init_t p_curl_easy_init = NULL;
static curl_easy_cleanup_t p_curl_easy_cleanup = NULL;
static curl_easy_setopt_t p_curl_easy_setopt = NULL;
static curl_easy_perform_t p_curl_easy_perform = NULL;
static curl_easy_getinfo_t p_curl_easy_getinfo = NULL;
static curl_slist_append_t p_curl_slist_append = NULL;
static curl_slist_free_all_t p_curl_slist_free_all = NULL;

static int curl_load(void)
{
    if (g_curl_lib)
        return 1;
    const char *libs[] = {"libcurl.so.4", "libcurl.so", "libcurl.dylib", NULL};
    for (int i = 0; libs[i]; i++)
    {
        if (vigil_platform_dlopen(libs[i], &g_curl_lib, NULL) == VIGIL_STATUS_OK)
            break;
    }
    if (!g_curl_lib)
        return 0;

    vigil_platform_dlsym(g_curl_lib, "curl_easy_init", (void **)&p_curl_easy_init, NULL);
    vigil_platform_dlsym(g_curl_lib, "curl_easy_cleanup", (void **)&p_curl_easy_cleanup, NULL);
    vigil_platform_dlsym(g_curl_lib, "curl_easy_setopt", (void **)&p_curl_easy_setopt, NULL);
    vigil_platform_dlsym(g_curl_lib, "curl_easy_perform", (void **)&p_curl_easy_perform, NULL);
    vigil_platform_dlsym(g_curl_lib, "curl_easy_getinfo", (void **)&p_curl_easy_getinfo, NULL);
    vigil_platform_dlsym(g_curl_lib, "curl_slist_append", (void **)&p_curl_slist_append, NULL);
    vigil_platform_dlsym(g_curl_lib, "curl_slist_free_all", (void **)&p_curl_slist_free_all, NULL);

    return p_curl_easy_init && p_curl_easy_cleanup && p_curl_easy_setopt && p_curl_easy_perform && p_curl_easy_getinfo;
}

typedef struct
{
    char *data;
    size_t len;
    size_t cap;
    size_t limit; /* 0 = unlimited */
} curl_buf_t;

static size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *ud)
{
    curl_buf_t *b = (curl_buf_t *)ud;
    size_t total = size * nmemb;
    if (b->limit > 0 && b->len + total > b->limit)
        return 0; /* signal error to curl */
    if (b->len + total >= b->cap)
    {
        b->cap = (b->len + total) * 2;
        b->data = (char *)realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, ptr, total);
    b->len += total;
    return total;
}

VIGIL_API vigil_status_t vigil_platform_http_request(const char *method, const char *url, const char *headers,
                                                     const char *body, size_t body_len, vigil_http_response_t *out,
                                                     vigil_error_t *error)
{
    if (!method || !url || !out)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INVALID_ARGUMENT;
            error->value = "null argument";
            error->length = 13;
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));

    if (!curl_load())
    {
        if (error)
        {
            error->type = VIGIL_STATUS_UNSUPPORTED;
            error->value = "libcurl not found; install libcurl or use plain HTTP fallback";
            error->length = 61;
        }
        return VIGIL_STATUS_UNSUPPORTED;
    }

    CURL *curl = p_curl_easy_init();
    if (!curl)
        return VIGIL_STATUS_INTERNAL;

    curl_buf_t buf = {(char *)malloc(4096), 0, 4096, 0};
    curl_buf_t hdr_buf = {(char *)malloc(2048), 0, 2048, CURL_HDR_LIMIT};
    p_curl_easy_setopt(curl, CURLOPT_URL, url);
    p_curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    p_curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    p_curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_write_cb);
    p_curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hdr_buf);
    p_curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    p_curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
    p_curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    struct curl_slist *hdr_list = NULL;
    if (headers && *headers)
    {
        char *hcopy = strdup(headers);
        char *saveptr = NULL;
        char *line = strtok_r(hcopy, "\r\n", &saveptr);
        while (line)
        {
            if (*line)
                hdr_list = p_curl_slist_append(hdr_list, line);
            line = strtok_r(NULL, "\r\n", &saveptr);
        }
        free(hcopy);
        if (hdr_list)
            p_curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr_list);
    }

    if (body && body_len > 0)
    {
        p_curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        p_curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
    }

    CURLcode res = p_curl_easy_perform(curl);
    if (res == CURLE_OK)
    {
        long code = 0;
        p_curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        out->status_code = (int)code;
        buf.data[buf.len] = '\0';
        out->body = buf.data;
        out->body_len = buf.len;
        hdr_buf.data[hdr_buf.len] = '\0';
        out->headers = hdr_buf.data;
        out->headers_len = hdr_buf.len;
    }
    else
    {
        free(buf.data);
        free(hdr_buf.data);
        if (hdr_list)
            p_curl_slist_free_all(hdr_list);
        p_curl_easy_cleanup(curl);
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "curl request failed";
            error->length = 19;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    if (hdr_list)
        p_curl_slist_free_all(hdr_list);
    p_curl_easy_cleanup(curl);
    return VIGIL_STATUS_OK;
}

/* ── TLS certificate store ──────────────────────────────────────── */

/* TARGET_OS_OSX is 1 only on macOS; it is 0 on iOS, tvOS, watchOS, etc.
 * SecTrustCopyAnchorCertificates exists only in the macOS SDK, so we must
 * guard with TARGET_OS_OSX and not just __APPLE__. */
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && defined(TARGET_OS_OSX) && TARGET_OS_OSX

#include <Security/Security.h>
/* Security.h transitively pulls in ObjC headers that define nil.
 * Undefine it here so the rest of this pure-C translation unit is unaffected. */
#ifdef nil
#undef nil
#endif

int vigil_platform_enumerate_tls_cas(vigil_tls_ca_cb_t cb, void *userdata)
{
    CFArrayRef anchors = NULL;
    if (SecTrustCopyAnchorCertificates(&anchors) != errSecSuccess || !anchors)
        return -1;
    CFIndex n = CFArrayGetCount(anchors);
    int count = 0;
    for (CFIndex i = 0; i < n; i++)
    {
        SecCertificateRef cert = (SecCertificateRef)CFArrayGetValueAtIndex(anchors, i);
        CFDataRef data = SecCertificateCopyData(cert);
        if (!data)
            continue;
        cb(CFDataGetBytePtr(data), (size_t)CFDataGetLength(data), userdata);
        CFRelease(data);
        count++;
    }
    CFRelease(anchors);
    return count;
}

#else /* Linux / BSD / iOS: probe well-known PEM bundle paths */

static const char *const tls_ca_paths_[] = {
    "/etc/ssl/certs/ca-certificates.crt",                /* Debian / Ubuntu */
    "/etc/ssl/cert.pem",                                 /* Alpine / OpenBSD */
    "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem", /* RHEL / Fedora */
    "/etc/ssl/ca-bundle.pem",                            /* SUSE */
    NULL,
};

static int tls_b64val_(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

static size_t tls_b64decode_(const char *b64, size_t b64_len, unsigned char *out, size_t max_out)
{
    size_t out_len = 0, i = 0;
    while (i + 3 < b64_len && out_len + 3 <= max_out)
    {
        int v0 = tls_b64val_((unsigned char)b64[i]);
        int v1 = tls_b64val_((unsigned char)b64[i + 1]);
        int v2 = tls_b64val_((unsigned char)b64[i + 2]);
        int v3 = tls_b64val_((unsigned char)b64[i + 3]);
        if (v0 < 0 || v1 < 0)
            break;
        out[out_len++] = (unsigned char)((v0 << 2) | (v1 >> 4));
        if (v2 < 0)
            break;
        out[out_len++] = (unsigned char)((v1 << 4) | (v2 >> 2));
        if (v3 < 0)
            break;
        out[out_len++] = (unsigned char)((v2 << 6) | v3);
        i += 4;
    }
    return out_len;
}

static int tls_pem_next_cert_(FILE *f, unsigned char *out, size_t *out_len, size_t max_len)
{
    char line[256];
    char b64[65536];
    size_t b64_len = 0;
    int in_cert = 0;

    while (fgets(line, (int)sizeof(line), f))
    {
        size_t llen = strcspn(line, "\r\n");
        if (llen >= sizeof(line))
            llen = sizeof(line) - 1;
        line[llen] = '\0';
        if (!in_cert)
        {
            if (strncmp(line, "-----BEGIN CERTIFICATE-----", 27) == 0 ||
                strncmp(line, "-----BEGIN X509 CERTIFICATE-----", 32) == 0)
            {
                in_cert = 1;
                b64_len = 0;
            }
        }
        else if (strncmp(line, "-----END", 8) == 0)
        {
            *out_len = tls_b64decode_(b64, b64_len, out, max_len);
            return (*out_len > 0) ? 1 : 0;
        }
        else if (b64_len + llen < sizeof(b64))
        {
            memcpy(b64 + b64_len, line, llen);
            b64_len += llen;
        }
    }
    return 0;
}

int vigil_platform_enumerate_tls_cas(vigil_tls_ca_cb_t cb, void *userdata)
{
    size_t i;
    for (i = 0; tls_ca_paths_[i] != NULL; i++)
    {
        FILE *f = fopen(tls_ca_paths_[i], "rb");
        if (!f)
            continue;
        unsigned char der[8192];
        size_t der_len = 0;
        int count = 0;
        while (tls_pem_next_cert_(f, der, &der_len, sizeof(der)))
        {
            cb(der, der_len, userdata);
            count++;
        }
        fclose(f);
        return count;
    }
    return -1;
}

#endif /* TARGET_OS_OSX */
