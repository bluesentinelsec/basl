/* VIGIL standard library: time module.
 *
 * Provides date/time operations using portable C time functions.
 * All timestamps are Unix timestamps (seconds since 1970-01-01 UTC).
 */

/* Enable strptime on glibc */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

/* Disable MSVC warnings for sscanf */
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_nanbox.h"
#include "platform/platform.h"

/* ── Helpers ─────────────────────────────────────────────────────── */

static int64_t get_i64_arg(vigil_vm_t *vm, size_t base, size_t idx)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    return vigil_nanbox_decode_int(v);
}

static int32_t get_i32_arg(vigil_vm_t *vm, size_t base, size_t idx)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    return vigil_nanbox_decode_i32(v);
}

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

static vigil_status_t push_bool(vigil_vm_t *vm, int val, vigil_error_t *error)
{
    vigil_value_t v;
    vigil_value_init_bool(&v, val);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t push_string(vigil_vm_t *vm, const char *str, size_t len, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_status_t s = vigil_string_object_new(vigil_vm_runtime(vm), str, len, &obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t v;
    vigil_value_init_object(&v, &obj);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

/* ── time.now() -> i64 ───────────────────────────────────────────── */

static vigil_status_t time_now(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    return push_i64(vm, (int64_t)time(NULL), error);
}

/* ── time.now_ms() -> i64 ────────────────────────────────────────── */

static vigil_status_t time_now_ms(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    return push_i64(vm, vigil_platform_now_ms(), error);
}

/* ── time.now_ns() -> i64 ────────────────────────────────────────── */

static vigil_status_t time_now_ns(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    return push_i64(vm, vigil_platform_now_ns(), error);
}

/* ── time.sleep(ms: i64) ─────────────────────────────────────────── */

static vigil_status_t time_sleep(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ms = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (ms > 0)
    {
        vigil_platform_thread_sleep((uint64_t)ms);
    }
    (void)error;
    return VIGIL_STATUS_OK;
}

/* ── Component extraction helpers ────────────────────────────────── */

static struct tm *get_local_tm(int64_t ts, struct tm *storage)
{
    time_t t = (time_t)ts;
#ifdef _WIN32
    if (localtime_s(storage, &t) != 0)
        return NULL;
    return storage;
#else
    (void)storage;
    return localtime(&t);
#endif
}

/* ── time.year(ts: i64) -> i32 ───────────────────────────────────── */

static vigil_status_t time_year(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_year + 1900 : 0, error);
}

/* ── time.month(ts: i64) -> i32 ──────────────────────────────────── */

static vigil_status_t time_month(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_mon + 1 : 0, error);
}

/* ── time.day(ts: i64) -> i32 ────────────────────────────────────── */

static vigil_status_t time_day(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_mday : 0, error);
}

/* ── time.hour(ts: i64) -> i32 ───────────────────────────────────── */

static vigil_status_t time_hour(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_hour : 0, error);
}

/* ── time.minute(ts: i64) -> i32 ─────────────────────────────────── */

static vigil_status_t time_minute(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_min : 0, error);
}

/* ── time.second(ts: i64) -> i32 ─────────────────────────────────── */

static vigil_status_t time_second(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_sec : 0, error);
}

/* ── time.weekday(ts: i64) -> i32 ────────────────────────────────── */

static vigil_status_t time_weekday(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_wday : 0, error);
}

/* ── time.yearday(ts: i64) -> i32 ────────────────────────────────── */

static vigil_status_t time_yearday(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_yday + 1 : 0, error);
}

/* ── time.is_dst(ts: i64) -> bool ────────────────────────────────── */

static vigil_status_t time_is_dst(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    return push_bool(vm, tm && tm->tm_isdst > 0, error);
}

/* ── time.utc_offset() -> i32 ────────────────────────────────────── */

static vigil_status_t time_utc_offset(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    time_t now = time(NULL);
    struct tm local_tm, utc_tm;
    int32_t offset;
    (void)arg_count;

#ifdef _WIN32
    localtime_s(&local_tm, &now);
    gmtime_s(&utc_tm, &now);
#else
    local_tm = *localtime(&now);
    utc_tm = *gmtime(&now);
#endif

    offset = (int32_t)((local_tm.tm_hour - utc_tm.tm_hour) * 3600 + (local_tm.tm_min - utc_tm.tm_min) * 60);
    /* Handle day boundary */
    if (local_tm.tm_mday != utc_tm.tm_mday)
    {
        if (local_tm.tm_mday > utc_tm.tm_mday || (local_tm.tm_mday == 1 && utc_tm.tm_mday > 1))
        {
            offset += 86400;
        }
        else
        {
            offset -= 86400;
        }
    }
    return push_i32(vm, offset, error);
}

/* ── time.date(y, m, d, h, min, s) -> i64 ────────────────────────── */

static vigil_status_t time_date(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t y = get_i32_arg(vm, base, 0);
    int32_t m = get_i32_arg(vm, base, 1);
    int32_t d = get_i32_arg(vm, base, 2);
    int32_t h = get_i32_arg(vm, base, 3);
    int32_t min = get_i32_arg(vm, base, 4);
    int32_t s = get_i32_arg(vm, base, 5);
    struct tm tm_val;
    time_t result;

    vigil_vm_stack_pop_n(vm, arg_count);

    memset(&tm_val, 0, sizeof(tm_val));
    tm_val.tm_year = y - 1900;
    tm_val.tm_mon = m - 1;
    tm_val.tm_mday = d;
    tm_val.tm_hour = h;
    tm_val.tm_min = min;
    tm_val.tm_sec = s;
    tm_val.tm_isdst = -1;

    result = mktime(&tm_val);
    return push_i64(vm, (int64_t)result, error);
}

/* ── time.format(ts: i64, fmt: string) -> string ─────────────────── */

static vigil_status_t time_format(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    const char *fmt;
    size_t fmt_len;
    struct tm storage, *tm;
    char buf[256];
    char fmt_buf[128];
    size_t len;

    if (!get_string_arg(vm, base, 1, &fmt, &fmt_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    tm = get_local_tm(ts, &storage);
    if (!tm)
    {
        return push_string(vm, "", 0, error);
    }

    /* Copy format to null-terminated buffer */
    if (fmt_len >= sizeof(fmt_buf))
        fmt_len = sizeof(fmt_buf) - 1;
    memcpy(fmt_buf, fmt, fmt_len);
    fmt_buf[fmt_len] = '\0';

    len = strftime(buf, sizeof(buf), fmt_buf, tm);
    return push_string(vm, buf, len, error);
}

/* ── time.parse(s: string, fmt: string) -> i64 ───────────────────── */

#ifndef _WIN32
/* strptime is POSIX, not available on Windows */
static vigil_status_t time_parse(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *str, *fmt;
    size_t str_len, fmt_len;
    struct tm tm_val;
    char str_buf[256], fmt_buf[128];
    time_t result;

    if (!get_string_arg(vm, base, 0, &str, &str_len) || !get_string_arg(vm, base, 1, &fmt, &fmt_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (str_len >= sizeof(str_buf))
        str_len = sizeof(str_buf) - 1;
    memcpy(str_buf, str, str_len);
    str_buf[str_len] = '\0';

    if (fmt_len >= sizeof(fmt_buf))
        fmt_len = sizeof(fmt_buf) - 1;
    memcpy(fmt_buf, fmt, fmt_len);
    fmt_buf[fmt_len] = '\0';

    memset(&tm_val, 0, sizeof(tm_val));
    tm_val.tm_isdst = -1;

    if (strptime(str_buf, fmt_buf, &tm_val) == NULL)
    {
        return push_i64(vm, -1, error);
    }

    result = mktime(&tm_val);
    return push_i64(vm, (int64_t)result, error);
}
#else
/* Windows fallback: parse ISO 8601 format only */
static vigil_status_t time_parse(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *str, *fmt;
    size_t str_len, fmt_len;
    struct tm tm_val;
    char str_buf[256];
    time_t result;
    int y, m, d, h, min, s;

    if (!get_string_arg(vm, base, 0, &str, &str_len) || !get_string_arg(vm, base, 1, &fmt, &fmt_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (str_len >= sizeof(str_buf))
        str_len = sizeof(str_buf) - 1;
    memcpy(str_buf, str, str_len);
    str_buf[str_len] = '\0';

    /* Try ISO 8601 format */
    if (sscanf(str_buf, "%d-%d-%dT%d:%d:%d", &y, &m, &d, &h, &min, &s) == 6 ||
        sscanf(str_buf, "%d-%d-%d %d:%d:%d", &y, &m, &d, &h, &min, &s) == 6)
    {
        memset(&tm_val, 0, sizeof(tm_val));
        tm_val.tm_year = y - 1900;
        tm_val.tm_mon = m - 1;
        tm_val.tm_mday = d;
        tm_val.tm_hour = h;
        tm_val.tm_min = min;
        tm_val.tm_sec = s;
        tm_val.tm_isdst = -1;
        result = mktime(&tm_val);
        return push_i64(vm, (int64_t)result, error);
    }

    /* Try date only */
    if (sscanf(str_buf, "%d-%d-%d", &y, &m, &d) == 3)
    {
        memset(&tm_val, 0, sizeof(tm_val));
        tm_val.tm_year = y - 1900;
        tm_val.tm_mon = m - 1;
        tm_val.tm_mday = d;
        tm_val.tm_isdst = -1;
        result = mktime(&tm_val);
        return push_i64(vm, (int64_t)result, error);
    }

    return push_i64(vm, -1, error);
}
#endif

/* ── Arithmetic functions ────────────────────────────────────────── */

static vigil_status_t time_add_days(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    int32_t n = get_i32_arg(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, ts + (int64_t)n * 86400, error);
}

static vigil_status_t time_add_hours(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    int32_t n = get_i32_arg(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, ts + (int64_t)n * 3600, error);
}

static vigil_status_t time_add_minutes(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    int32_t n = get_i32_arg(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, ts + (int64_t)n * 60, error);
}

static vigil_status_t time_add_seconds(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    int64_t n = get_i64_arg(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, ts + n, error);
}

static vigil_status_t time_diff_days(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t a = get_i64_arg(vm, base, 0);
    int64_t b = get_i64_arg(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, (a - b) / 86400, error);
}

/* ── Module definition ───────────────────────────────────────────── */

static const int i64_param[] = {VIGIL_TYPE_I64};
static const int i64_i32_param[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32};
static const int i64_i64_param[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64};
static const int i64_str_param[] = {VIGIL_TYPE_I64, VIGIL_TYPE_STRING};
static const int str_str_param[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int date_params[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                  VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};

static const vigil_native_module_function_t time_functions[] = {
    {"now", 3U, time_now, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U},
    {"now_ms", 6U, time_now_ms, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U},
    {"now_ns", 6U, time_now_ns, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U},
    {"sleep", 5U, time_sleep, 1U, i64_param, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U},
    {"year", 4U, time_year, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U},
    {"month", 5U, time_month, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U},
    {"day", 3U, time_day, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U},
    {"hour", 4U, time_hour, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U},
    {"minute", 6U, time_minute, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U},
    {"second", 6U, time_second, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U},
    {"weekday", 7U, time_weekday, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U},
    {"yearday", 7U, time_yearday, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U},
    {"is_dst", 6U, time_is_dst, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U},
    {"utc_offset", 10U, time_utc_offset, 0U, NULL, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U},
    {"date", 4U, time_date, 6U, date_params, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U},
    {"format", 6U, time_format, 2U, i64_str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U},
    {"parse", 5U, time_parse, 2U, str_str_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U},
    {"add_days", 8U, time_add_days, 2U, i64_i32_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U},
    {"add_hours", 9U, time_add_hours, 2U, i64_i32_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U},
    {"add_minutes", 11U, time_add_minutes, 2U, i64_i32_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U},
    {"add_seconds", 11U, time_add_seconds, 2U, i64_i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U},
    {"diff_days", 9U, time_diff_days, 2U, i64_i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U},
};

#define TIME_FUNCTION_COUNT (sizeof(time_functions) / sizeof(time_functions[0]))

VIGIL_API const vigil_native_module_t vigil_stdlib_time = {"time", 4U, time_functions, TIME_FUNCTION_COUNT, NULL, 0U};
