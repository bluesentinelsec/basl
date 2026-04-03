/* VIGIL standard library: log module.
 *
 * Structured logging with levels, formats, and custom handler support.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vigil/native_module.h"
#include "vigil/stdlib.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_nanbox.h"
#include "platform/platform.h"

/* ── Log levels ──────────────────────────────────────────────────── */

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_ERROR 3

/* ── Output formats ──────────────────────────────────────────────── */

#define LOG_FORMAT_TEXT 0
#define LOG_FORMAT_JSON 1

/* ── Time formats ────────────────────────────────────────────────── */

#define LOG_TIME_RFC3339 0
#define LOG_TIME_UNIX 1
#define LOG_TIME_NONE 2

/* ── Global state ────────────────────────────────────────────────── */

static int g_min_level = LOG_LEVEL_INFO;
static int g_format = LOG_FORMAT_TEXT;
static int g_time_format = LOG_TIME_RFC3339;
static FILE *g_output = NULL; /* NULL means stderr */
static char g_output_path[1024] = {0};

/* Custom handler support */
static vigil_log_handler_t g_custom_handler = NULL;
static void *g_custom_handler_data = NULL;

/* Logger handles with preset attributes */
#define MAX_LOGGERS 256
#define MAX_ATTRS_LEN 4096

static char g_logger_attrs[MAX_LOGGERS][MAX_ATTRS_LEN];
static volatile int64_t g_logger_count = 0;

/* ── Public C API ────────────────────────────────────────────────── */

void vigil_log_set_handler(vigil_log_handler_t handler, void *user_data)
{
    g_custom_handler = handler;
    g_custom_handler_data = user_data;
}

/* ── Helpers ─────────────────────────────────────────────────────── */

static vigil_status_t push_i64(vigil_vm_t *vm, int64_t val, vigil_error_t *error)
{
    vigil_value_t v;
    vigil_value_init_int(&v, val);
    return vigil_vm_stack_push(vm, &v, error);
}

static FILE *get_output(void)
{
    return g_output ? g_output : stderr;
}

static const char *level_name(int level)
{
    switch (level)
    {
    case LOG_LEVEL_DEBUG:
        return "DEBUG";
    case LOG_LEVEL_INFO:
        return "INFO";
    case LOG_LEVEL_WARN:
        return "WARN";
    case LOG_LEVEL_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

static void format_time(char *buf, size_t len)
{
    time_t now;
    struct tm *tm_info;
#ifdef _WIN32
    struct tm tm_storage;
#endif

    if (g_time_format == LOG_TIME_NONE)
    {
        buf[0] = '\0';
        return;
    }

    now = time(NULL);
    if (g_time_format == LOG_TIME_UNIX)
    {
        snprintf(buf, len, "%lld", (long long)now);
        return;
    }

    /* RFC3339 */
#ifdef _WIN32
    localtime_s(&tm_storage, &now);
    tm_info = &tm_storage;
#else
    tm_info = localtime(&now);
#endif
    if (tm_info)
    {
        strftime(buf, len, "%Y-%m-%dT%H:%M:%S", tm_info);
    }
    else
    {
        snprintf(buf, len, "%lld", (long long)now);
    }
}

static void escape_json_string(const char *src, char *dst, size_t dst_len)
{
    size_t i = 0;
    while (*src && i < dst_len - 2)
    {
        if (*src == '"' || *src == '\\')
        {
            dst[i++] = '\\';
        }
        dst[i++] = *src++;
    }
    dst[i] = '\0';
}

static void get_value_as_string(vigil_value_t v, char *buf, size_t len)
{
    if (vigil_nanbox_is_int(v))
    {
        snprintf(buf, len, "%lld", (long long)vigil_nanbox_decode_int(v));
    }
    else if (vigil_nanbox_is_double(v))
    {
        snprintf(buf, len, "%g", vigil_nanbox_decode_double(v));
    }
    else if (vigil_nanbox_is_bool(v))
    {
        snprintf(buf, len, "%s", vigil_nanbox_decode_bool(v) ? "true" : "false");
    }
    else if (vigil_nanbox_is_nil(v))
    {
        snprintf(buf, len, "nil");
    }
    else if (vigil_nanbox_is_object(v))
    {
        const vigil_object_t *obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(v);
        if (obj && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
        {
            const char *text = vigil_string_object_c_str(obj);
            if (text)
            {
                snprintf(buf, len, "%s", text);
                return;
            }
        }
        snprintf(buf, len, "<object>");
    }
    else
    {
        snprintf(buf, len, "<unknown>");
    }
}

/* Build JSON attrs string from key-value pairs */
static void build_attrs_json(vigil_vm_t *vm, size_t base, size_t start, size_t count, const char *preset_attrs,
                             char *out, size_t out_len)
{
    size_t pos = 0;
    size_t i;
    char key[256], val[1024], escaped[2048];

    out[pos++] = '{';

    /* Add preset attrs if any */
    if (preset_attrs && preset_attrs[0])
    {
        size_t plen = strlen(preset_attrs);
        if (pos + plen < out_len - 1)
        {
            memcpy(out + pos, preset_attrs, plen);
            pos += plen;
        }
    }

    /* Add inline attrs */
    for (i = start; i + 1 < start + count; i += 2)
    {
        get_value_as_string(vigil_vm_stack_get(vm, base + i), key, sizeof(key));
        get_value_as_string(vigil_vm_stack_get(vm, base + i + 1), val, sizeof(val));

        if (pos > 1)
        {
            out[pos++] = ',';
        }

        escape_json_string(key, escaped, sizeof(escaped));
        pos += (size_t)snprintf(out + pos, out_len - pos, "\"%s\":", escaped);

        /* Check if value looks like a number or bool */
        if (vigil_nanbox_is_int(vigil_vm_stack_get(vm, base + i + 1)) ||
            vigil_nanbox_is_double(vigil_vm_stack_get(vm, base + i + 1)) ||
            vigil_nanbox_is_bool(vigil_vm_stack_get(vm, base + i + 1)))
        {
            pos += (size_t)snprintf(out + pos, out_len - pos, "%s", val);
        }
        else
        {
            escape_json_string(val, escaped, sizeof(escaped));
            pos += (size_t)snprintf(out + pos, out_len - pos, "\"%s\"", escaped);
        }

        if (pos >= out_len - 2)
            break;
    }

    out[pos++] = '}';
    out[pos] = '\0';
}

/* ── Native callbacks ────────────────────────────────────────────── */

static vigil_status_t log_at_level(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error, int level,
                                   int64_t logger_handle)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    size_t msg_idx = (logger_handle >= 0) ? 1 : 0;
    char msg[1024] = "";
    char attrs_json[MAX_ATTRS_LEN];
    char time_buf[64];
    const char *preset_attrs = NULL;
    FILE *out;

    (void)error;

    if (level < g_min_level)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return VIGIL_STATUS_OK;
    }

    /* Get message */
    if (arg_count > msg_idx)
    {
        get_value_as_string(vigil_vm_stack_get(vm, base + msg_idx), msg, sizeof(msg));
    }

    /* Get preset attrs for this logger */
    if (logger_handle >= 0 && logger_handle < g_logger_count && logger_handle < MAX_LOGGERS)
    {
        preset_attrs = g_logger_attrs[logger_handle];
    }

    /* Build attrs JSON */
    build_attrs_json(vm, base, msg_idx + 1, arg_count - msg_idx - 1,
                     (preset_attrs && g_format == LOG_FORMAT_JSON) ? preset_attrs : NULL, attrs_json,
                     sizeof(attrs_json));

    /* Custom handler */
    if (g_custom_handler)
    {
        g_custom_handler(level, msg, attrs_json, g_custom_handler_data);
        vigil_vm_stack_pop_n(vm, arg_count);
        return VIGIL_STATUS_OK;
    }

    format_time(time_buf, sizeof(time_buf));
    out = get_output();

    if (g_format == LOG_FORMAT_JSON)
    {
        char escaped_msg[2048];
        escape_json_string(msg, escaped_msg, sizeof(escaped_msg));

        fprintf(out, "{");
        if (time_buf[0])
        {
            if (g_time_format == LOG_TIME_UNIX)
            {
                fprintf(out, "\"time\":%s,", time_buf);
            }
            else
            {
                fprintf(out, "\"time\":\"%s\",", time_buf);
            }
        }
        fprintf(out, "\"level\":\"%s\",\"msg\":\"%s\"", level_name(level), escaped_msg);
        if (strlen(attrs_json) > 2)
        {
            fprintf(out, ",%.*s", (int)(strlen(attrs_json) - 2), attrs_json + 1);
        }
        fprintf(out, "}\n");
    }
    else
    {
        /* Text format: time level msg key=value key=value ... */
        size_t i;
        if (time_buf[0])
        {
            fprintf(out, "%s ", time_buf);
        }
        fprintf(out, "%s %s", level_name(level), msg);

        /* Preset attrs (stored as text for text format) */
        if (preset_attrs && preset_attrs[0] && g_format == LOG_FORMAT_TEXT)
        {
            fprintf(out, " %s", preset_attrs);
        }

        /* Inline attrs */
        for (i = msg_idx + 1; i + 1 < arg_count; i += 2)
        {
            char key[256], val[1024];
            get_value_as_string(vigil_vm_stack_get(vm, base + i), key, sizeof(key));
            get_value_as_string(vigil_vm_stack_get(vm, base + i + 1), val, sizeof(val));
            fprintf(out, " %s=%s", key, val);
        }
        fprintf(out, "\n");
    }

    fflush(out);
    vigil_vm_stack_pop_n(vm, arg_count);
    return VIGIL_STATUS_OK;
}

/* Basic logging functions */
static vigil_status_t log_debug(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return log_at_level(vm, arg_count, error, LOG_LEVEL_DEBUG, -1);
}

static vigil_status_t log_info(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return log_at_level(vm, arg_count, error, LOG_LEVEL_INFO, -1);
}

static vigil_status_t log_warn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return log_at_level(vm, arg_count, error, LOG_LEVEL_WARN, -1);
}

static vigil_status_t log_error_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return log_at_level(vm, arg_count, error, LOG_LEVEL_ERROR, -1);
}

/* Logger-based logging functions */
static vigil_status_t log_debug_l(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, base));
    return log_at_level(vm, arg_count, error, LOG_LEVEL_DEBUG, handle);
}

static vigil_status_t log_info_l(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, base));
    return log_at_level(vm, arg_count, error, LOG_LEVEL_INFO, handle);
}

static vigil_status_t log_warn_l(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, base));
    return log_at_level(vm, arg_count, error, LOG_LEVEL_WARN, handle);
}

static vigil_status_t log_error_l(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, base));
    return log_at_level(vm, arg_count, error, LOG_LEVEL_ERROR, handle);
}

/* Configuration functions */
static vigil_status_t log_set_level(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *level_str = NULL;
    vigil_value_t v;

    (void)error;

    if (arg_count > 0)
    {
        v = vigil_vm_stack_get(vm, base);
        if (vigil_nanbox_is_object(v))
        {
            const vigil_object_t *obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(v);
            if (obj && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
            {
                level_str = vigil_string_object_c_str(obj);
            }
        }
    }

    if (level_str)
    {
        if (strcmp(level_str, "debug") == 0)
            g_min_level = LOG_LEVEL_DEBUG;
        else if (strcmp(level_str, "info") == 0)
            g_min_level = LOG_LEVEL_INFO;
        else if (strcmp(level_str, "warn") == 0)
            g_min_level = LOG_LEVEL_WARN;
        else if (strcmp(level_str, "error") == 0)
            g_min_level = LOG_LEVEL_ERROR;
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    return VIGIL_STATUS_OK;
}

static vigil_status_t log_set_format(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *fmt_str = NULL;
    vigil_value_t v;

    (void)error;

    if (arg_count > 0)
    {
        v = vigil_vm_stack_get(vm, base);
        if (vigil_nanbox_is_object(v))
        {
            const vigil_object_t *obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(v);
            if (obj && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
            {
                fmt_str = vigil_string_object_c_str(obj);
            }
        }
    }

    if (fmt_str)
    {
        if (strcmp(fmt_str, "text") == 0)
            g_format = LOG_FORMAT_TEXT;
        else if (strcmp(fmt_str, "json") == 0)
            g_format = LOG_FORMAT_JSON;
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    return VIGIL_STATUS_OK;
}

static vigil_status_t log_set_output(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *out_str = NULL;
    vigil_value_t v;

    (void)error;

    if (arg_count > 0)
    {
        v = vigil_vm_stack_get(vm, base);
        if (vigil_nanbox_is_object(v))
        {
            const vigil_object_t *obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(v);
            if (obj && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
            {
                out_str = vigil_string_object_c_str(obj);
            }
        }
    }

    if (out_str)
    {
        /* Close previous file if any */
        if (g_output && g_output != stdout && g_output != stderr)
        {
            fclose(g_output);
            g_output = NULL;
        }

        if (strcmp(out_str, "stdout") == 0)
        {
            g_output = stdout;
            g_output_path[0] = '\0';
        }
        else if (strcmp(out_str, "stderr") == 0)
        {
            g_output = NULL; /* NULL means stderr */
            g_output_path[0] = '\0';
        }
        else
        {
            /* File path */
#ifdef _WIN32
            if (fopen_s(&g_output, out_str, "a") != 0)
            {
                g_output = NULL;
            }
#else
            g_output = fopen(out_str, "a");
#endif
            if (g_output)
            {
                snprintf(g_output_path, sizeof(g_output_path), "%s", out_str);
            }
        }
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    return VIGIL_STATUS_OK;
}

static vigil_status_t log_set_time_format(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *tf_str = NULL;
    vigil_value_t v;

    (void)error;

    if (arg_count > 0)
    {
        v = vigil_vm_stack_get(vm, base);
        if (vigil_nanbox_is_object(v))
        {
            const vigil_object_t *obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(v);
            if (obj && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
            {
                tf_str = vigil_string_object_c_str(obj);
            }
        }
    }

    if (tf_str)
    {
        if (strcmp(tf_str, "rfc3339") == 0)
            g_time_format = LOG_TIME_RFC3339;
        else if (strcmp(tf_str, "unix") == 0)
            g_time_format = LOG_TIME_UNIX;
        else if (strcmp(tf_str, "none") == 0)
            g_time_format = LOG_TIME_NONE;
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    return VIGIL_STATUS_OK;
}

/* Create logger with preset attributes */
static vigil_status_t log_with(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle;
    size_t i, pos = 0;
    char *attrs;

    handle = vigil_atomic_add(&g_logger_count, 1);
    if (handle >= MAX_LOGGERS)
    {
        vigil_atomic_sub(&g_logger_count, 1);
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }

    attrs = g_logger_attrs[handle];
    attrs[0] = '\0';

    /* Store attrs in both text and JSON-compatible format */
    for (i = 0; i + 1 < arg_count; i += 2)
    {
        char key[256], val[1024];
        get_value_as_string(vigil_vm_stack_get(vm, base + i), key, sizeof(key));
        get_value_as_string(vigil_vm_stack_get(vm, base + i + 1), val, sizeof(val));

        if (g_format == LOG_FORMAT_JSON)
        {
            char escaped_key[512], escaped_val[2048];
            escape_json_string(key, escaped_key, sizeof(escaped_key));
            escape_json_string(val, escaped_val, sizeof(escaped_val));
            if (pos > 0)
            {
                pos += (size_t)snprintf(attrs + pos, MAX_ATTRS_LEN - pos, ",");
            }
            pos += (size_t)snprintf(attrs + pos, MAX_ATTRS_LEN - pos, "\"%s\":\"%s\"", escaped_key, escaped_val);
        }
        else
        {
            if (pos > 0)
            {
                pos += (size_t)snprintf(attrs + pos, MAX_ATTRS_LEN - pos, " ");
            }
            pos += (size_t)snprintf(attrs + pos, MAX_ATTRS_LEN - pos, "%s=%s", key, val);
        }

        if (pos >= MAX_ATTRS_LEN - 1)
            break;
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, handle, error);
}

/* ── Module descriptor ───────────────────────────────────────────── */

static const int str_param[] = {VIGIL_TYPE_STRING};
static const int str_str_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int i64_str_params[] = {VIGIL_TYPE_I64, VIGIL_TYPE_STRING};
static const char *const log_msg_param_names[] = {"msg"};
static const char *const log_logger_msg_param_names[] = {"logger", "msg"};
static const char *const log_level_param_names[] = {"level"};
static const char *const log_format_param_names[] = {"format"};
static const char *const log_dest_param_names[] = {"dest"};
static const char *const log_time_format_param_names[] = {"format"};
static const char *const log_with_param_names[] = {"key", "value"};

static const vigil_native_symbol_doc_t vigil_log_module_doc = {
    "Structured logging with levels and formats.",
    "The log module provides structured logging similar to Go's slog package. It supports debug/info/warn/error levels, text and JSON output, and custom handlers for embedders.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_log_debug_doc = {
    "Log at DEBUG level.",
    "Logs a message to the configured output.",
    "log.debug(\"starting parser\")",
};

static const vigil_native_symbol_doc_t vigil_log_info_doc = {
    "Log at INFO level.",
    "Logs a message to the configured output.",
    "log.info(\"request received\")",
};

static const vigil_native_symbol_doc_t vigil_log_warn_doc = {
    "Log at WARN level.",
    "Logs a message to the configured output.",
    "log.warn(\"slow query detected\")",
};

static const vigil_native_symbol_doc_t vigil_log_error_doc = {
    "Log at ERROR level.",
    "Logs a message to the configured output.",
    "log.error(\"connection failed\")",
};

static const vigil_native_symbol_doc_t vigil_log_debug_l_doc = {
    "Log at DEBUG with logger.",
    "Uses preset attributes from a logger handle created by log.with.",
    "log.debug_l(logger, \"msg\")",
};

static const vigil_native_symbol_doc_t vigil_log_info_l_doc = {
    "Log at INFO with logger.",
    "Uses preset attributes from a logger handle created by log.with.",
    "log.info_l(logger, \"msg\")",
};

static const vigil_native_symbol_doc_t vigil_log_warn_l_doc = {
    "Log at WARN with logger.",
    "Uses preset attributes from a logger handle created by log.with.",
    "log.warn_l(logger, \"msg\")",
};

static const vigil_native_symbol_doc_t vigil_log_error_l_doc = {
    "Log at ERROR with logger.",
    "Uses preset attributes from a logger handle created by log.with.",
    "log.error_l(logger, \"msg\")",
};

static const vigil_native_symbol_doc_t vigil_log_set_level_doc = {
    "Set minimum log level.",
    "Valid levels are \"debug\", \"info\", \"warn\", and \"error\". The default is \"info\".",
    "log.set_level(\"debug\")",
};

static const vigil_native_symbol_doc_t vigil_log_set_format_doc = {
    "Set output format.",
    "Valid formats are \"text\" and \"json\".",
    "log.set_format(\"json\")",
};

static const vigil_native_symbol_doc_t vigil_log_set_output_doc = {
    "Set output destination.",
    "Valid destinations are \"stdout\", \"stderr\", or a file path.",
    "log.set_output(\"app.log\")",
};

static const vigil_native_symbol_doc_t vigil_log_set_time_format_doc = {
    "Set timestamp format.",
    "Valid formats are \"rfc3339\", \"unix\", and \"none\".",
    "log.set_time_format(\"unix\")",
};

static const vigil_native_symbol_doc_t vigil_log_with_doc = {
    "Create logger with preset attribute.",
    "Returns a logger handle for use with the *_l logging functions.",
    "i64 logger = log.with(\"service\", \"api\")",
};

static const vigil_native_module_function_t log_functions[] = {
    {"debug", 5U, log_debug, 1U, str_param, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U, log_msg_param_names, NULL,
     NULL, &vigil_log_debug_doc},
    {"info", 4U, log_info, 1U, str_param, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U, log_msg_param_names, NULL,
     NULL, &vigil_log_info_doc},
    {"warn", 4U, log_warn, 1U, str_param, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U, log_msg_param_names, NULL,
     NULL, &vigil_log_warn_doc},
    {"error", 5U, log_error_fn, 1U, str_param, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U, log_msg_param_names, NULL,
     NULL, &vigil_log_error_doc},
    {"debug_l", 7U, log_debug_l, 2U, i64_str_params, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U,
     log_logger_msg_param_names, NULL, NULL, &vigil_log_debug_l_doc},
    {"info_l", 6U, log_info_l, 2U, i64_str_params, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U,
     log_logger_msg_param_names, NULL, NULL, &vigil_log_info_l_doc},
    {"warn_l", 6U, log_warn_l, 2U, i64_str_params, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U,
     log_logger_msg_param_names, NULL, NULL, &vigil_log_warn_l_doc},
    {"error_l", 7U, log_error_l, 2U, i64_str_params, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U,
     log_logger_msg_param_names, NULL, NULL, &vigil_log_error_l_doc},
    {"set_level", 9U, log_set_level, 1U, str_param, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U,
     log_level_param_names, NULL, NULL, &vigil_log_set_level_doc},
    {"set_format", 10U, log_set_format, 1U, str_param, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U,
     log_format_param_names, NULL, NULL, &vigil_log_set_format_doc},
    {"set_output", 10U, log_set_output, 1U, str_param, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U,
     log_dest_param_names, NULL, NULL, &vigil_log_set_output_doc},
    {"set_time_format", 15U, log_set_time_format, 1U, str_param, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U,
     log_time_format_param_names, NULL, NULL, &vigil_log_set_time_format_doc},
    {"with", 4U, log_with, 2U, str_str_params, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, log_with_param_names, NULL,
     NULL, &vigil_log_with_doc},
};

#define LOG_FUNCTION_COUNT (sizeof(log_functions) / sizeof(log_functions[0]))

VIGIL_API const vigil_native_module_t vigil_stdlib_log = {"log", 3U, log_functions, LOG_FUNCTION_COUNT, NULL, 0U,
                                                          &vigil_log_module_doc,
    NULL, 0U};
