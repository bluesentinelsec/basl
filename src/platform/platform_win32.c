#include "platform.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <wincrypt.h>
#include <windows.h>

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
    (void)allocator;
    FILE *f = NULL;
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

    if (fopen_s(&f, path, "rb") != 0 || !f)
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
    FILE *f = NULL;
    if (!path)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "platform: NULL path");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (fopen_s(&f, path, "wb") != 0 || !f)
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
    DWORD attr;
    if (!path || !out_exists)
        return VIGIL_STATUS_INVALID_ARGUMENT;
    attr = GetFileAttributesA(path);
    *out_exists = (attr != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_platform_is_directory(const char *path, int *out_is_dir)
{
    DWORD attr;
    if (!path || !out_is_dir)
        return VIGIL_STATUS_INVALID_ARGUMENT;
    attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        *out_is_dir = 0;
        return VIGIL_STATUS_OK;
    }
    *out_is_dir = (attr & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_platform_mkdir(const char *path, vigil_error_t *error)
{
    if (!path)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "platform: NULL path");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (_mkdir(path) != 0 && errno != EEXIST)
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
        if (buf[i] == '/' || buf[i] == '\\' || buf[i] == '\0')
        {
            char saved = buf[i];
            buf[i] = '\0';
            /* Skip drive letter root like "C:" */
            if (i == 2 && buf[1] == ':')
            {
                buf[i] = saved;
                continue;
            }
            if (_mkdir(buf) != 0 && errno != EEXIST)
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
    DWORD attr;
    if (!path)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "platform: NULL path");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        if (_rmdir(path) != 0)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "platform: rmdir failed");
            return VIGIL_STATUS_INTERNAL;
        }
        return VIGIL_STATUS_OK;
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
    DWORD len;
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
    len = GetModuleFileNameA(NULL, out_buf, (DWORD)buf_size);
    if (len == 0 || len >= (DWORD)buf_size)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "GetModuleFileName failed";
            error->length = 24;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_platform_make_executable(const char *path, vigil_error_t *error)
{
    (void)path;
    (void)error;
    return VIGIL_STATUS_OK; /* no-op on Windows */
}

vigil_status_t vigil_platform_list_dir(const char *path, vigil_platform_dir_callback_t callback, void *user_data,
                                       vigil_error_t *error)
{
    char pattern[4096];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "failed to open directory";
            error->length = 24;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    do
    {
        if (fd.cFileName[0] == '.' && (fd.cFileName[1] == '\0' || (fd.cFileName[1] == '.' && fd.cFileName[2] == '\0')))
        {
            continue;
        }
        int is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        vigil_status_t s = callback(fd.cFileName, is_dir, user_data);
        if (s != VIGIL_STATUS_OK)
            break;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return VIGIL_STATUS_OK;
}

/* ── Environment variables ───────────────────────────────────────── */

VIGIL_API vigil_status_t vigil_platform_getenv(const vigil_allocator_t *allocator, const char *name, char **out_value,
                                               int *out_found, vigil_error_t *error)
{
    (void)allocator;
    char buf[32767]; /* max env var size on Windows */
    DWORD len;
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
    len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND)
    {
        *out_value = NULL;
        *out_found = 0;
    }
    else
    {
        *out_value = _strdup(buf);
        *out_found = 1;
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
    if (_putenv_s(name, value) != 0)
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
    return "windows";
}

VIGIL_API vigil_status_t vigil_platform_getcwd(const vigil_allocator_t *allocator, char **out_path,
                                               vigil_error_t *error)
{
    (void)allocator;
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
    if (!_getcwd(buf, sizeof(buf)))
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "getcwd failed";
            error->length = 13;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    *out_path = _strdup(buf);
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_temp_dir(const vigil_allocator_t *allocator, char **out_path,
                                                 vigil_error_t *error)
{
    (void)allocator;
    char buf[MAX_PATH + 1];
    DWORD len;
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
    len = GetTempPathA(sizeof(buf), buf);
    if (len == 0)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "GetTempPath failed";
            error->length = 18;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    /* Remove trailing backslash if present. */
    if (len > 0 && (buf[len - 1] == '\\' || buf[len - 1] == '/'))
        buf[len - 1] = '\0';
    *out_path = _strdup(buf);
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_hostname(const vigil_allocator_t *allocator, char **out_name,
                                                 vigil_error_t *error)
{
    (void)allocator;
    char buf[256];
    DWORD size = sizeof(buf);
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
    if (!GetComputerNameA(buf, &size))
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "GetComputerName failed";
            error->length = 22;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    *out_name = _strdup(buf);
    return VIGIL_STATUS_OK;
}

/* ── Process execution ───────────────────────────────────────────── */

VIGIL_API vigil_status_t vigil_platform_exec(const vigil_allocator_t *allocator, const char *const *argv,
                                             char **out_stdout, char **out_stderr, int *out_exit_code,
                                             vigil_error_t *error)
{
    (void)allocator;
    HANDLE stdout_rd = NULL, stdout_wr = NULL;
    HANDLE stderr_rd = NULL, stderr_wr = NULL;
    SECURITY_ATTRIBUTES sa;
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    char cmdline[32768];
    size_t pos = 0;
    int i;
    DWORD exit_code;

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

    /* Build command line. */
    for (i = 0; argv[i]; i++)
    {
        if (i > 0 && pos < sizeof(cmdline) - 1)
            cmdline[pos++] = ' ';
        {
            size_t len = strlen(argv[i]);
            if (pos + len < sizeof(cmdline) - 1)
            {
                memcpy(cmdline + pos, argv[i], len);
                pos += len;
            }
        }
    }
    cmdline[pos] = '\0';

    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&stdout_rd, &stdout_wr, &sa, 0) || !CreatePipe(&stderr_rd, &stderr_wr, &sa, 0))
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "CreatePipe failed";
            error->length = 17;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    SetHandleInformation(stdout_rd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderr_rd, HANDLE_FLAG_INHERIT, 0);

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = stdout_wr;
    si.hStdError = stderr_wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        CloseHandle(stdout_rd);
        CloseHandle(stdout_wr);
        CloseHandle(stderr_rd);
        CloseHandle(stderr_wr);
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "CreateProcess failed";
            error->length = 20;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    CloseHandle(stdout_wr);
    CloseHandle(stderr_wr);

    /* Read pipes. */
    {
        char *bufs[2] = {NULL, NULL};
        size_t lens[2] = {0, 0};
        size_t caps[2] = {0, 0};
        HANDLE handles[2] = {stdout_rd, stderr_rd};
        int idx;
        for (idx = 0; idx < 2; idx++)
        {
            char tmp[4096];
            DWORD n;
            while (ReadFile(handles[idx], tmp, sizeof(tmp), &n, NULL) && n > 0)
            {
                if (lens[idx] + n >= caps[idx])
                {
                    caps[idx] = (lens[idx] + n) * 2 + 1;
                    bufs[idx] = realloc(bufs[idx], caps[idx]);
                }
                memcpy(bufs[idx] + lens[idx], tmp, n);
                lens[idx] += n;
            }
            CloseHandle(handles[idx]);
            if (!bufs[idx])
                bufs[idx] = calloc(1, 1);
            bufs[idx][lens[idx]] = '\0';
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &exit_code);
        *out_stdout = bufs[0];
        *out_stderr = bufs[1];
        *out_exit_code = (int)exit_code;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return VIGIL_STATUS_OK;
}

/* ── Dynamic library loading ─────────────────────────────────────── */

VIGIL_API vigil_status_t vigil_platform_dlopen(const char *path, void **out_handle, vigil_error_t *error)
{
    HMODULE h;
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
    h = LoadLibraryA(path);
    if (!h)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "LoadLibrary failed";
            error->length = 18;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    *out_handle = (void *)h;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_dlsym(void *handle, const char *name, void **out_sym, vigil_error_t *error)
{
    FARPROC sym;
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
    sym = GetProcAddress((HMODULE)handle, name);
    if (!sym)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "GetProcAddress failed";
            error->length = 21;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    *out_sym = (void *)sym;
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
    FreeLibrary((HMODULE)handle);
    return VIGIL_STATUS_OK;
}

/* ── Terminal raw mode ───────────────────────────────────────────── */

struct vigil_terminal_state
{
    DWORD orig_in_mode;
    DWORD orig_out_mode;
    HANDLE h_in;
    HANDLE h_out;
};

int vigil_platform_is_terminal(void)
{
    DWORD mode;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0;
}

vigil_status_t vigil_platform_terminal_raw(vigil_terminal_state_t **out_state, vigil_error_t *error)
{
    vigil_terminal_state_t *state = malloc(sizeof(*state));
    if (!state)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "out of memory");
        return VIGIL_STATUS_INTERNAL;
    }
    state->h_in = GetStdHandle(STD_INPUT_HANDLE);
    state->h_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(state->h_in, &state->orig_in_mode))
    {
        free(state);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "GetConsoleMode failed");
        return VIGIL_STATUS_INTERNAL;
    }
    GetConsoleMode(state->h_out, &state->orig_out_mode);
    SetConsoleMode(state->h_in, ENABLE_VIRTUAL_TERMINAL_INPUT);
    SetConsoleMode(state->h_out, state->orig_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    *out_state = state;
    return VIGIL_STATUS_OK;
}

void vigil_platform_terminal_restore(vigil_terminal_state_t *state)
{
    if (!state)
        return;
    SetConsoleMode(state->h_in, state->orig_in_mode);
    SetConsoleMode(state->h_out, state->orig_out_mode);
    free(state);
}

int vigil_platform_terminal_read_byte(void)
{
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    char c;
    DWORD n;
    if (!ReadFile(h, &c, 1, &n, NULL) || n == 0)
        return -1;
    return (unsigned char)c;
}

int vigil_platform_terminal_width(void)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
        return info.srWindow.Right - info.srWindow.Left + 1;
    return 80;
}

/* ── Extended filesystem operations ──────────────────────────────── */

VIGIL_API vigil_status_t vigil_platform_copy_file(const char *src, const char *dst, vigil_error_t *error)
{
    if (!CopyFileA(src, dst, FALSE))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "CopyFile failed");
        return VIGIL_STATUS_INTERNAL;
    }
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_rename(const char *src, const char *dst, vigil_error_t *error)
{
    if (!MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "MoveFileEx failed");
        return VIGIL_STATUS_INTERNAL;
    }
    return VIGIL_STATUS_OK;
}

VIGIL_API int64_t vigil_platform_file_size(const char *path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data))
        return -1;
    LARGE_INTEGER size;
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    return size.QuadPart;
}

VIGIL_API int64_t vigil_platform_file_mtime(const char *path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data))
        return -1;
    /* Convert FILETIME to Unix timestamp */
    ULARGE_INTEGER ull;
    ull.LowPart = data.ftLastWriteTime.dwLowDateTime;
    ull.HighPart = data.ftLastWriteTime.dwHighDateTime;
    return (int64_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

VIGIL_API vigil_status_t vigil_platform_is_file(const char *path, int *out_is_file)
{
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        *out_is_file = 0;
        return VIGIL_STATUS_OK;
    }
    *out_is_file = (attr & FILE_ATTRIBUTE_DIRECTORY) ? 0 : 1;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_symlink(const char *target, const char *linkpath, vigil_error_t *error)
{
    DWORD flags = 0;
    DWORD attr = GetFileAttributesA(target);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
    if (!CreateSymbolicLinkA(linkpath, target, flags))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "CreateSymbolicLink failed");
        return VIGIL_STATUS_INTERNAL;
    }
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_hardlink(const char *target, const char *linkpath, vigil_error_t *error)
{
    if (!CreateHardLinkA(linkpath, target, NULL))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "CreateHardLink failed");
        return VIGIL_STATUS_INTERNAL;
    }
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_readlink(const vigil_allocator_t *allocator, const char *path,
                                                 char **out_target, vigil_error_t *error)
{
    (void)allocator;
    (void)path;
    vigil_error_set_literal(error, VIGIL_STATUS_UNSUPPORTED, "readlink not fully supported on Windows");
    *out_target = NULL;
    return VIGIL_STATUS_UNSUPPORTED;
}

VIGIL_API vigil_status_t vigil_platform_is_symlink(const char *path, int *out_is_symlink)
{
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        *out_is_symlink = 0;
        return VIGIL_STATUS_OK;
    }
    *out_is_symlink = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) ? 1 : 0;
    return VIGIL_STATUS_OK;
}

static vigil_status_t win32_remove_all(const char *path, vigil_error_t *error)
{
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return VIGIL_STATUS_OK;
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        if (!DeleteFileA(path))
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "remove failed");
            return VIGIL_STATUS_INTERNAL;
        }
        return VIGIL_STATUS_OK;
    }
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
                continue;
            char child[MAX_PATH];
            snprintf(child, sizeof(child), "%s\\%s", path, fd.cFileName);
            vigil_status_t s = win32_remove_all(child, error);
            if (s != VIGIL_STATUS_OK)
            {
                FindClose(h);
                return s;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    if (!RemoveDirectoryA(path))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "rmdir failed");
        return VIGIL_STATUS_INTERNAL;
    }
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_remove_all(const char *path, vigil_error_t *error)
{
    return win32_remove_all(path, error);
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

VIGIL_API vigil_status_t vigil_platform_home_dir(const vigil_allocator_t *allocator, char **out_path,
                                                 vigil_error_t *error)
{
    (void)allocator;
    char *userprofile = getenv("USERPROFILE");
    if (userprofile)
    {
        *out_path = _strdup(userprofile);
        if (!*out_path)
            return VIGIL_STATUS_OUT_OF_MEMORY;
        return VIGIL_STATUS_OK;
    }
    vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "USERPROFILE not set");
    return VIGIL_STATUS_INTERNAL;
}

VIGIL_API vigil_status_t vigil_platform_config_dir(const vigil_allocator_t *allocator, char **out_path,
                                                   vigil_error_t *error)
{
    (void)allocator;
    char *appdata = getenv("APPDATA");
    if (appdata)
    {
        *out_path = _strdup(appdata);
        if (!*out_path)
            return VIGIL_STATUS_OUT_OF_MEMORY;
        return VIGIL_STATUS_OK;
    }
    vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "APPDATA not set");
    return VIGIL_STATUS_INTERNAL;
}

VIGIL_API vigil_status_t vigil_platform_cache_dir(const vigil_allocator_t *allocator, char **out_path,
                                                  vigil_error_t *error)
{
    (void)allocator;
    char *localappdata = getenv("LOCALAPPDATA");
    if (localappdata)
    {
        *out_path = _strdup(localappdata);
        if (!*out_path)
            return VIGIL_STATUS_OUT_OF_MEMORY;
        return VIGIL_STATUS_OK;
    }
    vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "LOCALAPPDATA not set");
    return VIGIL_STATUS_INTERNAL;
}

VIGIL_API vigil_status_t vigil_platform_data_dir(const vigil_allocator_t *allocator, char **out_path,
                                                 vigil_error_t *error)
{
    (void)allocator;
    return vigil_platform_config_dir(allocator, out_path, error);
}

VIGIL_API vigil_status_t vigil_platform_temp_file(const vigil_allocator_t *allocator, const char *prefix,
                                                  char **out_path, vigil_error_t *error)
{
    (void)allocator;
    char tmpdir[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, tmpdir))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "GetTempPath failed");
        return VIGIL_STATUS_INTERNAL;
    }
    char path[MAX_PATH];
    if (!GetTempFileNameA(tmpdir, prefix ? prefix : "tmp", 0, path))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "GetTempFileName failed");
        return VIGIL_STATUS_INTERNAL;
    }
    *out_path = _strdup(path);
    if (!*out_path)
        return VIGIL_STATUS_OUT_OF_MEMORY;
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
    HANDLE handle;
    vigil_thread_func_t func;
    void *arg;
    volatile int detached;
};

static DWORD WINAPI thread_wrapper(LPVOID arg)
{
    vigil_platform_thread_t *t = (vigil_platform_thread_t *)arg;
    t->func(t->arg);
    if (t->detached)
    {
        CloseHandle(t->handle);
        free(t);
    }
    return 0;
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
    t->handle = CreateThread(NULL, 0, thread_wrapper, t, 0, NULL);
    if (!t->handle)
    {
        free(t);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "CreateThread failed");
        return VIGIL_STATUS_INTERNAL;
    }
    *out_thread = t;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_thread_join(vigil_platform_thread_t *thread, vigil_error_t *error)
{
    if (WaitForSingleObject(thread->handle, INFINITE) == WAIT_FAILED)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "WaitForSingleObject failed");
        return VIGIL_STATUS_INTERNAL;
    }
    CloseHandle(thread->handle);
    free(thread);
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_thread_detach(vigil_platform_thread_t *thread, vigil_error_t *error)
{
    (void)error;
    thread->detached = 1; /* wrapper will close handle and free after func returns */
    return VIGIL_STATUS_OK;
}

VIGIL_API uint64_t vigil_platform_thread_current_id(void)
{
    return (uint64_t)GetCurrentThreadId();
}

VIGIL_API void vigil_platform_thread_yield(void)
{
    SwitchToThread();
}

VIGIL_API void vigil_platform_thread_sleep(uint64_t milliseconds)
{
    Sleep((DWORD)milliseconds);
}

VIGIL_API int64_t vigil_platform_now_ms(void)
{
    FILETIME ft;
    ULARGE_INTEGER uli;

    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return (int64_t)((uli.QuadPart - 116444736000000000ULL) / 10000ULL);
}

VIGIL_API int64_t vigil_platform_now_ns(void)
{
    FILETIME ft;
    ULARGE_INTEGER uli;

    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return (int64_t)((uli.QuadPart - 116444736000000000ULL) * 100ULL);
}

/* ── Mutex (using CRITICAL_SECTION for efficiency) ───────────────── */

struct vigil_platform_mutex
{
    CRITICAL_SECTION cs;
};

VIGIL_API vigil_status_t vigil_platform_mutex_create(vigil_platform_mutex_t **out_mutex, vigil_error_t *error)
{
    (void)error;
    vigil_platform_mutex_t *m = malloc(sizeof(*m));
    if (!m)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    InitializeCriticalSection(&m->cs);
    *out_mutex = m;
    return VIGIL_STATUS_OK;
}

VIGIL_API void vigil_platform_mutex_destroy(vigil_platform_mutex_t *mutex)
{
    if (mutex)
    {
        DeleteCriticalSection(&mutex->cs);
        free(mutex);
    }
}

VIGIL_API void vigil_platform_mutex_lock(vigil_platform_mutex_t *mutex)
{
    EnterCriticalSection(&mutex->cs);
}

VIGIL_API void vigil_platform_mutex_unlock(vigil_platform_mutex_t *mutex)
{
    LeaveCriticalSection(&mutex->cs);
}

VIGIL_API int vigil_platform_mutex_trylock(vigil_platform_mutex_t *mutex)
{
    return TryEnterCriticalSection(&mutex->cs) ? 1 : 0;
}

/* ── Condition variable ──────────────────────────────────────────── */

struct vigil_platform_cond
{
    CONDITION_VARIABLE cv;
};

VIGIL_API vigil_status_t vigil_platform_cond_create(vigil_platform_cond_t **out_cond, vigil_error_t *error)
{
    (void)error;
    vigil_platform_cond_t *c = malloc(sizeof(*c));
    if (!c)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    InitializeConditionVariable(&c->cv);
    *out_cond = c;
    return VIGIL_STATUS_OK;
}

VIGIL_API void vigil_platform_cond_destroy(vigil_platform_cond_t *cond)
{
    /* Windows condition variables don't need explicit destruction */
    free(cond);
}

VIGIL_API void vigil_platform_cond_wait(vigil_platform_cond_t *cond, vigil_platform_mutex_t *mutex)
{
    SleepConditionVariableCS(&cond->cv, &mutex->cs, INFINITE);
}

VIGIL_API int vigil_platform_cond_timedwait(vigil_platform_cond_t *cond, vigil_platform_mutex_t *mutex,
                                            uint64_t timeout_ms)
{
    return SleepConditionVariableCS(&cond->cv, &mutex->cs, (DWORD)timeout_ms) ? 1 : 0;
}

VIGIL_API void vigil_platform_cond_signal(vigil_platform_cond_t *cond)
{
    WakeConditionVariable(&cond->cv);
}

VIGIL_API void vigil_platform_cond_broadcast(vigil_platform_cond_t *cond)
{
    WakeAllConditionVariable(&cond->cv);
}

/* ── Read-write lock ─────────────────────────────────────────────── */

struct vigil_platform_rwlock
{
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE readers_cv;
    CONDITION_VARIABLE writers_cv;
    int readers;
    int writer_active;
    int waiting_writers;
    DWORD writer_owner;
};

VIGIL_API vigil_status_t vigil_platform_rwlock_create(vigil_platform_rwlock_t **out_rwlock, vigil_error_t *error)
{
    (void)error;
    vigil_platform_rwlock_t *rw = malloc(sizeof(*rw));
    if (!rw)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    InitializeCriticalSection(&rw->cs);
    InitializeConditionVariable(&rw->readers_cv);
    InitializeConditionVariable(&rw->writers_cv);
    rw->readers = 0;
    rw->writer_active = 0;
    rw->waiting_writers = 0;
    rw->writer_owner = 0;
    *out_rwlock = rw;
    return VIGIL_STATUS_OK;
}

VIGIL_API void vigil_platform_rwlock_destroy(vigil_platform_rwlock_t *rwlock)
{
    DeleteCriticalSection(&rwlock->cs);
    free(rwlock);
}

VIGIL_API void vigil_platform_rwlock_rdlock(vigil_platform_rwlock_t *rwlock)
{
    EnterCriticalSection(&rwlock->cs);
    while (rwlock->writer_active || rwlock->waiting_writers > 0)
    {
        SleepConditionVariableCS(&rwlock->readers_cv, &rwlock->cs, INFINITE);
    }
    rwlock->readers++;
    LeaveCriticalSection(&rwlock->cs);
}

VIGIL_API void vigil_platform_rwlock_wrlock(vigil_platform_rwlock_t *rwlock)
{
    EnterCriticalSection(&rwlock->cs);
    rwlock->waiting_writers++;
    while (rwlock->writer_active || rwlock->readers > 0)
    {
        SleepConditionVariableCS(&rwlock->writers_cv, &rwlock->cs, INFINITE);
    }
    rwlock->waiting_writers--;
    rwlock->writer_active = 1;
    rwlock->writer_owner = GetCurrentThreadId();
    LeaveCriticalSection(&rwlock->cs);
}

VIGIL_API void vigil_platform_rwlock_unlock(vigil_platform_rwlock_t *rwlock)
{
    EnterCriticalSection(&rwlock->cs);
    if (rwlock->writer_active && rwlock->writer_owner == GetCurrentThreadId())
    {
        rwlock->writer_active = 0;
        rwlock->writer_owner = 0;
        if (rwlock->waiting_writers > 0)
        {
            WakeConditionVariable(&rwlock->writers_cv);
        }
        else
        {
            WakeAllConditionVariable(&rwlock->readers_cv);
        }
    }
    else
    {
        if (rwlock->readers > 0)
        {
            rwlock->readers--;
            if (rwlock->readers == 0 && rwlock->waiting_writers > 0)
            {
                WakeConditionVariable(&rwlock->writers_cv);
            }
        }
    }
    LeaveCriticalSection(&rwlock->cs);
}

/* ── Thread-local storage ────────────────────────────────────────── */

struct vigil_platform_tls_key
{
    DWORD key;
};

VIGIL_API vigil_status_t vigil_platform_tls_create(vigil_platform_tls_key_t **out_key, void (*destructor)(void *),
                                                   vigil_error_t *error)
{
    (void)destructor; /* Windows TLS doesn't support destructors directly */
    vigil_platform_tls_key_t *k = malloc(sizeof(*k));
    if (!k)
        return VIGIL_STATUS_OUT_OF_MEMORY;
    k->key = TlsAlloc();
    if (k->key == TLS_OUT_OF_INDEXES)
    {
        free(k);
        vigil_error_set_literal(error, VIGIL_STATUS_INTERNAL, "TlsAlloc failed");
        return VIGIL_STATUS_INTERNAL;
    }
    *out_key = k;
    return VIGIL_STATUS_OK;
}

VIGIL_API void vigil_platform_tls_destroy(vigil_platform_tls_key_t *key)
{
    if (key)
    {
        TlsFree(key->key);
        free(key);
    }
}

VIGIL_API void vigil_platform_tls_set(vigil_platform_tls_key_t *key, void *value)
{
    TlsSetValue(key->key, value);
}

VIGIL_API void *vigil_platform_tls_get(vigil_platform_tls_key_t *key)
{
    return TlsGetValue(key->key);
}

/* ── Atomic operations ───────────────────────────────────────────── */

VIGIL_API int64_t vigil_atomic_load(const volatile int64_t *ptr)
{
    return InterlockedCompareExchange64((volatile LONG64 *)ptr, 0, 0);
}

VIGIL_API void vigil_atomic_store(volatile int64_t *ptr, int64_t value)
{
    InterlockedExchange64((volatile LONG64 *)ptr, value);
}

VIGIL_API int64_t vigil_atomic_add(volatile int64_t *ptr, int64_t value)
{
    return InterlockedExchangeAdd64((volatile LONG64 *)ptr, value);
}

VIGIL_API int64_t vigil_atomic_sub(volatile int64_t *ptr, int64_t value)
{
    return InterlockedExchangeAdd64((volatile LONG64 *)ptr, -value);
}

VIGIL_API int vigil_atomic_cas(volatile int64_t *ptr, int64_t expected, int64_t desired)
{
    return InterlockedCompareExchange64((volatile LONG64 *)ptr, desired, expected) == expected ? 1 : 0;
}

VIGIL_API int64_t vigil_atomic_exchange(volatile int64_t *ptr, int64_t value)
{
    return InterlockedExchange64((volatile LONG64 *)ptr, value);
}

VIGIL_API int64_t vigil_atomic_fetch_or(volatile int64_t *ptr, int64_t value)
{
    return InterlockedOr64((volatile LONG64 *)ptr, value);
}

VIGIL_API int64_t vigil_atomic_fetch_and(volatile int64_t *ptr, int64_t value)
{
    return InterlockedAnd64((volatile LONG64 *)ptr, value);
}

VIGIL_API int64_t vigil_atomic_fetch_xor(volatile int64_t *ptr, int64_t value)
{
    return InterlockedXor64((volatile LONG64 *)ptr, value);
}

VIGIL_API void vigil_atomic_fence(void)
{
    MemoryBarrier();
}

/* ── TCP sockets (Winsock, runtime-loaded) ───────────────────────── */

#include <winsock2.h>
#include <ws2tcpip.h>

typedef int(WSAAPI *pWSAStartup_t)(WORD, LPWSADATA);
typedef SOCKET(WSAAPI *pSocket_t)(int, int, int);
typedef int(WSAAPI *pBind_t)(SOCKET, const struct sockaddr *, int);
typedef int(WSAAPI *pListen_t)(SOCKET, int);
typedef SOCKET(WSAAPI *pAccept_t)(SOCKET, struct sockaddr *, int *);
typedef int(WSAAPI *pConnect_t)(SOCKET, const struct sockaddr *, int);
typedef int(WSAAPI *pSend_t)(SOCKET, const char *, int, int);
typedef int(WSAAPI *pRecv_t)(SOCKET, char *, int, int);
typedef int(WSAAPI *pSendto_t)(SOCKET, const char *, int, int, const struct sockaddr *, int);
typedef int(WSAAPI *pRecvfrom_t)(SOCKET, char *, int, int, struct sockaddr *, int *);
typedef int(WSAAPI *pClosesocket_t)(SOCKET);
typedef int(WSAAPI *pSetsockopt_t)(SOCKET, int, int, const char *, int);
typedef INT(WSAAPI *pGetaddrinfo_t)(PCSTR, PCSTR, const ADDRINFOA *, PADDRINFOA *);
typedef void(WSAAPI *pFreeaddrinfo_t)(PADDRINFOA);
typedef INT(WSAAPI *pInetPton_t)(INT, PCSTR, PVOID);
typedef u_short(WSAAPI *pHtons_t)(u_short);
typedef u_long(WSAAPI *pHtonl_t)(u_long);

static void *g_ws2_lib = NULL;
static pWSAStartup_t p_WSAStartup = NULL;
static pSocket_t p_socket = NULL;
static pBind_t p_bind = NULL;
static pListen_t p_listen = NULL;
static pAccept_t p_accept = NULL;
static pConnect_t p_connect = NULL;
static pSend_t p_send = NULL;
static pRecv_t p_recv = NULL;
static pSendto_t p_sendto = NULL;
static pRecvfrom_t p_recvfrom = NULL;
static pClosesocket_t p_closesocket = NULL;
static pSetsockopt_t p_setsockopt = NULL;
static pGetaddrinfo_t p_getaddrinfo = NULL;
static pFreeaddrinfo_t p_freeaddrinfo = NULL;
static pInetPton_t p_inet_pton = NULL;
static pHtons_t p_htons = NULL;
static pHtonl_t p_htonl = NULL;

static int ws2_loaded = 0;

static int ws2_load(void)
{
    if (ws2_loaded)
        return 1;
    if (vigil_platform_dlopen("ws2_32.dll", &g_ws2_lib, NULL) != VIGIL_STATUS_OK)
        return 0;

    vigil_platform_dlsym(g_ws2_lib, "WSAStartup", (void **)&p_WSAStartup, NULL);
    vigil_platform_dlsym(g_ws2_lib, "socket", (void **)&p_socket, NULL);
    vigil_platform_dlsym(g_ws2_lib, "bind", (void **)&p_bind, NULL);
    vigil_platform_dlsym(g_ws2_lib, "listen", (void **)&p_listen, NULL);
    vigil_platform_dlsym(g_ws2_lib, "accept", (void **)&p_accept, NULL);
    vigil_platform_dlsym(g_ws2_lib, "connect", (void **)&p_connect, NULL);
    vigil_platform_dlsym(g_ws2_lib, "send", (void **)&p_send, NULL);
    vigil_platform_dlsym(g_ws2_lib, "recv", (void **)&p_recv, NULL);
    vigil_platform_dlsym(g_ws2_lib, "sendto", (void **)&p_sendto, NULL);
    vigil_platform_dlsym(g_ws2_lib, "recvfrom", (void **)&p_recvfrom, NULL);
    vigil_platform_dlsym(g_ws2_lib, "closesocket", (void **)&p_closesocket, NULL);
    vigil_platform_dlsym(g_ws2_lib, "setsockopt", (void **)&p_setsockopt, NULL);
    vigil_platform_dlsym(g_ws2_lib, "getaddrinfo", (void **)&p_getaddrinfo, NULL);
    vigil_platform_dlsym(g_ws2_lib, "freeaddrinfo", (void **)&p_freeaddrinfo, NULL);
    vigil_platform_dlsym(g_ws2_lib, "inet_pton", (void **)&p_inet_pton, NULL);
    vigil_platform_dlsym(g_ws2_lib, "htons", (void **)&p_htons, NULL);
    vigil_platform_dlsym(g_ws2_lib, "htonl", (void **)&p_htonl, NULL);

    if (!p_socket || !p_bind || !p_listen || !p_accept || !p_connect || !p_send || !p_recv || !p_sendto ||
        !p_recvfrom || !p_closesocket || !p_WSAStartup)
        return 0;

    WSADATA wsa;
    p_WSAStartup(MAKEWORD(2, 2), &wsa);
    ws2_loaded = 1;
    return 1;
}

static u_short vigil_ws2_htons(u_short value)
{
    if (p_htons != NULL)
    {
        return p_htons(value);
    }
    return (u_short)(((value & 0x00ffU) << 8) | ((value & 0xff00U) >> 8));
}

static u_long vigil_ws2_htonl(u_long value)
{
    if (p_htonl != NULL)
    {
        return p_htonl(value);
    }
    return ((value & 0x000000ffUL) << 24) | ((value & 0x0000ff00UL) << 8) | ((value & 0x00ff0000UL) >> 8) |
           ((value & 0xff000000UL) >> 24);
}

VIGIL_API vigil_status_t vigil_platform_net_init(vigil_error_t *error)
{
    if (ws2_load())
        return VIGIL_STATUS_OK;
    if (error)
    {
        error->type = VIGIL_STATUS_UNSUPPORTED;
        error->value = "ws2_32.dll not found";
        error->length = 20;
    }
    return VIGIL_STATUS_UNSUPPORTED;
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
    if (!ws2_load())
    {
        if (error)
        {
            error->type = VIGIL_STATUS_UNSUPPORTED;
            error->value = "ws2_32.dll not found";
            error->length = 20;
        }
        return VIGIL_STATUS_UNSUPPORTED;
    }

    SOCKET fd = p_socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET)
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
    p_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = vigil_ws2_htons((u_short)port);
    if (strcmp(host, "0.0.0.0") == 0)
    {
        addr.sin_addr.s_addr = vigil_ws2_htonl(INADDR_ANY);
    }
    else if (p_inet_pton)
    {
        p_inet_pton(AF_INET, host, &addr.sin_addr);
    }

    if (p_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        p_closesocket(fd);
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "bind() failed";
            error->length = 13;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    if (p_listen(fd, 128) != 0)
    {
        p_closesocket(fd);
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
    if (!ws2_load())
        return VIGIL_STATUS_UNSUPPORTED;
    SOCKET client = p_accept((SOCKET)listener, NULL, NULL);
    if (client == INVALID_SOCKET)
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
    if (!ws2_load())
        return VIGIL_STATUS_UNSUPPORTED;

    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (p_getaddrinfo(host, port_str, &hints, &res) != 0)
    {
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "getaddrinfo() failed";
            error->length = 20;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    SOCKET fd = p_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == INVALID_SOCKET)
    {
        p_freeaddrinfo(res);
        return VIGIL_STATUS_INTERNAL;
    }

    if (p_connect(fd, res->ai_addr, (int)res->ai_addrlen) != 0)
    {
        p_closesocket(fd);
        p_freeaddrinfo(res);
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "connect() failed";
            error->length = 16;
        }
        return VIGIL_STATUS_INTERNAL;
    }
    p_freeaddrinfo(res);
    *out_sock = (vigil_socket_t)fd;
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_tcp_send(vigil_socket_t sock, const void *data, size_t len, size_t *out_sent,
                                                 vigil_error_t *error)
{
    (void)error;
    if (!ws2_load())
        return VIGIL_STATUS_UNSUPPORTED;
    int n = p_send((SOCKET)sock, (const char *)data, (int)len, 0);
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
    if (!ws2_load())
        return VIGIL_STATUS_UNSUPPORTED;
    int n = p_recv((SOCKET)sock, (char *)buf, (int)cap, 0);
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
    if (ws2_loaded && p_closesocket)
        p_closesocket((SOCKET)sock);
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_tcp_set_timeout(vigil_socket_t sock, int timeout_ms, vigil_error_t *error)
{
    (void)error;
    if (!ws2_load() || !p_setsockopt)
        return VIGIL_STATUS_UNSUPPORTED;
    DWORD tv = (DWORD)timeout_ms;
    p_setsockopt((SOCKET)sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    p_setsockopt((SOCKET)sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
    return VIGIL_STATUS_OK;
}

VIGIL_API vigil_status_t vigil_platform_udp_bind(const char *host, int port, vigil_socket_t *out_sock,
                                                 vigil_error_t *error)
{
    SOCKET fd;
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
    if (!ws2_load())
        return VIGIL_STATUS_UNSUPPORTED;

    fd = p_socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == INVALID_SOCKET)
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
    addr.sin_port = vigil_ws2_htons((u_short)port);
    if (host[0] == '\0' || strcmp(host, "0.0.0.0") == 0)
    {
        addr.sin_addr.s_addr = vigil_ws2_htonl(INADDR_ANY);
    }
    else if (!p_inet_pton || p_inet_pton(AF_INET, host, &addr.sin_addr) != 1)
    {
        p_closesocket(fd);
        if (error)
        {
            error->type = VIGIL_STATUS_INTERNAL;
            error->value = "inet_pton() failed";
            error->length = 18;
        }
        return VIGIL_STATUS_INTERNAL;
    }

    if (p_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        p_closesocket(fd);
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
    SOCKET fd;

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
    if (!ws2_load())
        return VIGIL_STATUS_UNSUPPORTED;

    fd = p_socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == INVALID_SOCKET)
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
    struct addrinfo hints = {0};
    struct addrinfo *res = NULL;
    char port_str[16];
    int sent;

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
    if (!ws2_load())
        return VIGIL_STATUS_UNSUPPORTED;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (!p_getaddrinfo || p_getaddrinfo(host, port_str, &hints, &res) != 0 || res == NULL)
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

    sent = p_sendto((SOCKET)sock, (const char *)data, (int)len, 0, res->ai_addr, (int)res->ai_addrlen);
    p_freeaddrinfo(res);
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
    int received;

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
    if (!ws2_load())
        return VIGIL_STATUS_UNSUPPORTED;

    received = p_recvfrom((SOCKET)sock, (char *)buf, (int)cap, 0, NULL, NULL);
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

/* ── HTTP client via WinHTTP (runtime-loaded) ────────────────────── */

#include <winhttp.h>

typedef HINTERNET(WINAPI *pWinHttpOpen_t)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef HINTERNET(WINAPI *pWinHttpConnect_t)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
typedef HINTERNET(WINAPI *pWinHttpOpenRequest_t)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR *, DWORD);
typedef BOOL(WINAPI *pWinHttpSendRequest_t)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
typedef BOOL(WINAPI *pWinHttpReceiveResponse_t)(HINTERNET, LPVOID);
typedef BOOL(WINAPI *pWinHttpQueryHeaders_t)(HINTERNET, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD);
typedef BOOL(WINAPI *pWinHttpReadData_t)(HINTERNET, LPVOID, DWORD, LPDWORD);
typedef BOOL(WINAPI *pWinHttpCloseHandle_t)(HINTERNET);

static void *g_winhttp_lib = NULL;
static pWinHttpOpen_t pw_Open = NULL;
static pWinHttpConnect_t pw_Connect = NULL;
static pWinHttpOpenRequest_t pw_OpenRequest = NULL;
static pWinHttpSendRequest_t pw_SendRequest = NULL;
static pWinHttpReceiveResponse_t pw_ReceiveResponse = NULL;
static pWinHttpQueryHeaders_t pw_QueryHeaders = NULL;
static pWinHttpReadData_t pw_ReadData = NULL;
static pWinHttpCloseHandle_t pw_CloseHandle = NULL;

static int winhttp_load(void)
{
    if (g_winhttp_lib)
        return 1;
    if (vigil_platform_dlopen("winhttp.dll", &g_winhttp_lib, NULL) != VIGIL_STATUS_OK)
        return 0;

    vigil_platform_dlsym(g_winhttp_lib, "WinHttpOpen", (void **)&pw_Open, NULL);
    vigil_platform_dlsym(g_winhttp_lib, "WinHttpConnect", (void **)&pw_Connect, NULL);
    vigil_platform_dlsym(g_winhttp_lib, "WinHttpOpenRequest", (void **)&pw_OpenRequest, NULL);
    vigil_platform_dlsym(g_winhttp_lib, "WinHttpSendRequest", (void **)&pw_SendRequest, NULL);
    vigil_platform_dlsym(g_winhttp_lib, "WinHttpReceiveResponse", (void **)&pw_ReceiveResponse, NULL);
    vigil_platform_dlsym(g_winhttp_lib, "WinHttpQueryHeaders", (void **)&pw_QueryHeaders, NULL);
    vigil_platform_dlsym(g_winhttp_lib, "WinHttpReadData", (void **)&pw_ReadData, NULL);
    vigil_platform_dlsym(g_winhttp_lib, "WinHttpCloseHandle", (void **)&pw_CloseHandle, NULL);

    return pw_Open && pw_Connect && pw_OpenRequest && pw_SendRequest && pw_ReceiveResponse && pw_QueryHeaders &&
           pw_ReadData && pw_CloseHandle;
}

static void win_str_tolower(char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
    {
        if (s[i] >= 'A' && s[i] <= 'Z')
            s[i] = (char)(s[i] + 32);
    }
}

#define WINHTTP_HDR_LIMIT (256U * 1024U)

/* Minimal URL parser for WinHTTP (scheme, host, port, path). */
static int winhttp_parse_url(const char *url, char *scheme, size_t scheme_sz, char *host, size_t host_sz, int *port,
                             char *path, size_t path_sz)
{
    const char *p = url;
    const char *se = strstr(p, "://");
    if (se)
    {
        size_t slen = (size_t)(se - p);
        if (slen >= scheme_sz)
            return 0;
        memcpy(scheme, p, slen);
        scheme[slen] = '\0';
        win_str_tolower(scheme, slen); /* normalise to lowercase */
        p = se + 3;
    }
    else
    {
        memcpy(scheme, "http", 5);
    }
    const char *ps = strchr(p, '/');
    const char *pp = strchr(p, ':');
    const char *he = ps ? ps : p + strlen(p);
    if (pp && pp < he)
        he = pp;
    size_t hlen = (size_t)(he - p);
    if (hlen >= host_sz)
        return 0;
    memcpy(host, p, hlen);
    host[hlen] = '\0';
    if (pp && pp < (ps ? ps : p + strlen(p)))
    {
        *port = atoi(pp + 1);
    }
    else
    {
        *port = (strcmp(scheme, "https") == 0) ? 443 : 80;
    }
    if (ps)
    {
        size_t plen = strlen(ps);
        if (plen >= path_sz)
            return 0; /* path too long — fail rather than silently truncate */
        memcpy(path, ps, plen);
        path[plen] = '\0';
    }
    else
    {
        memcpy(path, "/", 2);
    }
    return 1;
}

static void winhttp_read_body(HINTERNET req, vigil_http_response_t *out)
{
    size_t cap = 8192, len = 0;
    char *buf = (char *)malloc(cap);
    DWORD downloaded;
    while (buf && pw_ReadData(req, buf + len, (DWORD)(cap - len - 1), &downloaded) && downloaded > 0)
    {
        len += downloaded;
        if (len + 1 >= cap)
        {
            cap *= 2;
            buf = (char *)realloc(buf, cap);
        }
    }
    if (buf)
    {
        buf[len] = '\0';
        out->body = buf;
        out->body_len = len;
    }
}

VIGIL_API vigil_status_t vigil_platform_http_request(const vigil_allocator_t *allocator, const char *method,
                                                     const char *url, const char *headers, const char *body,
                                                     size_t body_len, vigil_http_response_t *out, vigil_error_t *error)
{
    (void)allocator;
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

    if (!winhttp_load())
    {
        if (error)
        {
            error->type = VIGIL_STATUS_UNSUPPORTED;
            error->value = "winhttp.dll not found; install Windows HTTP services or use plain HTTP fallback";
            error->length = 79;
        }
        return VIGIL_STATUS_UNSUPPORTED;
    }

    char scheme[16], host[256], path[2048];
    int port;
    if (!winhttp_parse_url(url, scheme, sizeof(scheme), host, sizeof(host), &port, path, sizeof(path)))
        return VIGIL_STATUS_INVALID_ARGUMENT;

    wchar_t whost[256], wpath[2048], wmethod[16];
    MultiByteToWideChar(CP_UTF8, 0, host, -1, whost, 256);
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 2048);
    MultiByteToWideChar(CP_UTF8, 0, method, -1, wmethod, 16);

    HINTERNET session =
        pw_Open(L"VIGIL/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        return VIGIL_STATUS_INTERNAL;

    HINTERNET conn = pw_Connect(session, whost, (INTERNET_PORT)port, 0);
    if (!conn)
    {
        pw_CloseHandle(session);
        return VIGIL_STATUS_INTERNAL;
    }

    DWORD flags = (strcmp(scheme, "https") == 0) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = pw_OpenRequest(conn, wmethod, wpath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req)
    {
        pw_CloseHandle(conn);
        pw_CloseHandle(session);
        return VIGIL_STATUS_INTERNAL;
    }

    wchar_t wheaders[4096] = L"";
    if (headers && *headers)
        MultiByteToWideChar(CP_UTF8, 0, headers, -1, wheaders, 4096);

    BOOL ok = pw_SendRequest(req, wheaders[0] ? wheaders : WINHTTP_NO_ADDITIONAL_HEADERS, (DWORD)-1, (LPVOID)body,
                             (DWORD)body_len, (DWORD)body_len, 0);
    if (!ok)
        goto fail;

    ok = pw_ReceiveResponse(req, NULL);
    if (!ok)
        goto fail;

    {
        DWORD status = 0, sz = sizeof(status);
        pw_QueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &sz, WINHTTP_NO_HEADER_INDEX);
        out->status_code = (int)status;
    }

    /* Capture response headers */
    {
        DWORD hdr_sz = 0;
        pw_QueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &hdr_sz,
                        WINHTTP_NO_HEADER_INDEX);
        if (hdr_sz > 0 && hdr_sz <= WINHTTP_HDR_LIMIT)
        {
            wchar_t *whdr = (wchar_t *)malloc(hdr_sz);
            if (whdr && pw_QueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, whdr,
                                        &hdr_sz, WINHTTP_NO_HEADER_INDEX))
            {
                int utf8_len = WideCharToMultiByte(CP_UTF8, 0, whdr, -1, NULL, 0, NULL, NULL);
                if (utf8_len > 0)
                {
                    out->headers = (char *)malloc((size_t)utf8_len);
                    WideCharToMultiByte(CP_UTF8, 0, whdr, -1, out->headers, utf8_len, NULL, NULL);
                    out->headers_len = (size_t)(utf8_len - 1);
                }
            }
            free(whdr);
        }
    }

    winhttp_read_body(req, out);

    pw_CloseHandle(req);
    pw_CloseHandle(conn);
    pw_CloseHandle(session);
    return VIGIL_STATUS_OK;

fail:
    pw_CloseHandle(req);
    pw_CloseHandle(conn);
    pw_CloseHandle(session);
    if (error)
    {
        error->type = VIGIL_STATUS_INTERNAL;
        error->value = "WinHTTP request failed";
        error->length = 22;
    }
    return VIGIL_STATUS_INTERNAL;
}

/* ── TLS certificate store ──────────────────────────────────────── */

int vigil_platform_enumerate_tls_cas(vigil_tls_ca_cb_t cb, void *userdata)
{
    HCERTSTORE store = CertOpenSystemStoreA(0, "ROOT");
    if (!store)
        return -1;
    int count = 0;
    PCCERT_CONTEXT cert = NULL;
    while ((cert = CertEnumCertificatesInStore(store, cert)) != NULL)
    {
        cb(cert->pbCertEncoded, (size_t)cert->cbCertEncoded, userdata);
        count++;
    }
    CertCloseStore(store, 0);
    return count;
}
