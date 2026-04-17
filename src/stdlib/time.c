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
#include "vigil/runtime.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_internal.h"
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

static struct tm *get_utc_tm(int64_t ts, struct tm *storage)
{
    time_t t = (time_t)ts;
#ifdef _WIN32
    if (gmtime_s(storage, &t) != 0) return NULL;
    return storage;
#else
    (void)storage;
    return gmtime(&t);
#endif
}

/* ── push_i64_and_ok / push_i64_and_err helpers ──────────────────── */

static vigil_status_t push_i64_and_ok(vigil_vm_t *vm, int64_t val, vigil_error_t *error)
{
    vigil_status_t s = push_i64(vm, val, error);
    if (s != VIGIL_STATUS_OK) return s;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t push_i64_and_err(vigil_vm_t *vm, const char *msg, vigil_error_t *error)
{
    vigil_status_t s = push_i64(vm, 0, error);
    if (s != VIGIL_STATUS_OK) return s;
    vigil_object_t *obj = NULL;
    s = vigil_error_object_new_cstr(vigil_vm_runtime(vm), msg, 1, &obj, error);
    if (s != VIGIL_STATUS_OK) return s;
    vigil_value_t v;
    vigil_value_init_object(&v, &obj);
    s = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return s;
}

/* ── UTC offset computation helper ───────────────────────────────── */

static int32_t compute_utc_offset_for(int64_t ts)
{
    time_t t = (time_t)ts;
    struct tm local_tm, utc_tm;
    int32_t offset;
#ifdef _WIN32
    localtime_s(&local_tm, &t);
    gmtime_s(&utc_tm, &t);
#else
    local_tm = *localtime(&t);
    utc_tm = *gmtime(&t);
#endif
    offset = (int32_t)((local_tm.tm_hour - utc_tm.tm_hour) * 3600 +
                        (local_tm.tm_min - utc_tm.tm_min) * 60);
    if (local_tm.tm_mday != utc_tm.tm_mday)
    {
        if (local_tm.tm_mday > utc_tm.tm_mday || (local_tm.tm_mday == 1 && utc_tm.tm_mday > 1))
            offset += 86400;
        else
            offset -= 86400;
    }
    return offset;
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

/* ── UTC component functions ─────────────────────────────────────── */

static vigil_status_t time_utc_year(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_utc_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_year + 1900 : 0, error);
}

static vigil_status_t time_utc_month(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_utc_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_mon + 1 : 0, error);
}

static vigil_status_t time_utc_day(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_utc_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_mday : 0, error);
}

static vigil_status_t time_utc_hour(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_utc_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_hour : 0, error);
}

static vigil_status_t time_utc_minute(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_utc_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_min : 0, error);
}

static vigil_status_t time_utc_second(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_utc_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_sec : 0, error);
}

static vigil_status_t time_utc_weekday(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_utc_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_wday : 0, error);
}

static vigil_status_t time_utc_yearday(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_utc_tm(ts, &storage);
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

/* ── time.utc_offset(ts: i64) -> i32 ─────────────────────────────── */

static vigil_status_t time_utc_offset(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i32(vm, compute_utc_offset_for(ts), error);
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

/* ── time.parse(s: string, fmt: string) -> (i64, err) ─────────────── */

#ifdef _WIN32
/* Portable strptime for Windows — handles common format codes. */
static char *vigil_strptime(const char *s, const char *fmt, struct tm *tm)
{
    static const char *const abbrev_days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char *const abbrev_months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                                 "Jul","Aug","Sep","Oct","Nov","Dec"};
    while (*fmt)
    {
        if (*fmt == '%')
        {
            fmt++;
            switch (*fmt)
            {
            case 'Y':
                if (sscanf(s, "%d", &tm->tm_year) != 1) return NULL;
                tm->tm_year -= 1900;
                while (*s && (*s == '-' || (*s >= '0' && *s <= '9'))) s++;
                break;
            case 'm':
                if (sscanf(s, "%d", &tm->tm_mon) != 1) return NULL;
                tm->tm_mon -= 1;
                while (*s >= '0' && *s <= '9') s++;
                break;
            case 'd':
                if (sscanf(s, "%d", &tm->tm_mday) != 1) return NULL;
                while (*s >= '0' && *s <= '9') s++;
                break;
            case 'H':
                if (sscanf(s, "%d", &tm->tm_hour) != 1) return NULL;
                while (*s >= '0' && *s <= '9') s++;
                break;
            case 'I':
                if (sscanf(s, "%d", &tm->tm_hour) != 1) return NULL;
                while (*s >= '0' && *s <= '9') s++;
                break;
            case 'M':
                if (sscanf(s, "%d", &tm->tm_min) != 1) return NULL;
                while (*s >= '0' && *s <= '9') s++;
                break;
            case 'S':
                if (sscanf(s, "%d", &tm->tm_sec) != 1) return NULL;
                while (*s >= '0' && *s <= '9') s++;
                break;
            case 'j':
                if (sscanf(s, "%d", &tm->tm_yday) != 1) return NULL;
                tm->tm_yday -= 1;
                while (*s >= '0' && *s <= '9') s++;
                break;
            case 'a': {
                int i, found = 0;
                for (i = 0; i < 7; i++) {
                    size_t len = strlen(abbrev_days[i]);
                    if (strncmp(s, abbrev_days[i], len) == 0) {
                        tm->tm_wday = i; s += len; found = 1; break;
                    }
                }
                if (!found) return NULL;
                break;
            }
            case 'b': {
                int i, found = 0;
                for (i = 0; i < 12; i++) {
                    size_t len = strlen(abbrev_months[i]);
                    if (strncmp(s, abbrev_months[i], len) == 0) {
                        tm->tm_mon = i; s += len; found = 1; break;
                    }
                }
                if (!found) return NULL;
                break;
            }
            case 'p':
                if (strncmp(s, "PM", 2) == 0 || strncmp(s, "pm", 2) == 0) {
                    if (tm->tm_hour < 12) tm->tm_hour += 12;
                    s += 2;
                } else if (strncmp(s, "AM", 2) == 0 || strncmp(s, "am", 2) == 0) {
                    if (tm->tm_hour == 12) tm->tm_hour = 0;
                    s += 2;
                } else {
                    return NULL;
                }
                break;
            case 'n':
                if (*s == '\n') s++;
                break;
            case 't':
                if (*s == '\t') s++;
                break;
            case '%':
                if (*s != '%') return NULL;
                s++;
                break;
            default:
                return NULL;
            }
            fmt++;
        }
        else
        {
            if (*s != *fmt) return NULL;
            s++; fmt++;
        }
    }
    return (char *)s;
}

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
        return push_i64_and_err(vm, "parse: invalid arguments", error);
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

    if (vigil_strptime(str_buf, fmt_buf, &tm_val) == NULL)
    {
        return push_i64_and_err(vm, "parse: format mismatch", error);
    }

    result = mktime(&tm_val);
    return push_i64_and_ok(vm, (int64_t)result, error);
}
#else
/* POSIX: strptime available */
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
        return push_i64_and_err(vm, "parse: invalid arguments", error);
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
        return push_i64_and_err(vm, "parse: format mismatch", error);
    }

    result = mktime(&tm_val);
    return push_i64_and_ok(vm, (int64_t)result, error);
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

/* ── time.diff_hours(a: i64, b: i64) -> i64 ──────────────────────── */

static vigil_status_t time_diff_hours(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t a = get_i64_arg(vm, base, 0);
    int64_t b = get_i64_arg(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, (a - b) / 3600, error);
}

/* ── time.diff_minutes(a: i64, b: i64) -> i64 ────────────────────── */

static vigil_status_t time_diff_minutes(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t a = get_i64_arg(vm, base, 0);
    int64_t b = get_i64_arg(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, (a - b) / 60, error);
}

/* ── time.diff_seconds(a: i64, b: i64) -> i64 ────────────────────── */

static vigil_status_t time_diff_seconds(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t a = get_i64_arg(vm, base, 0);
    int64_t b = get_i64_arg(vm, base, 1);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, a - b, error);
}

/* ── time.add_months(ts: i64, n: i32) -> i64 ─────────────────────── */

static int days_in_month(int year, int mon)
{
    static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (mon < 0 || mon > 11) return 30;
    if (mon == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        return 29;
    return dim[mon];
}

static vigil_status_t time_add_months(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    int32_t n = get_i32_arg(vm, base, 1);
    struct tm storage, *tm;
    struct tm val;
    time_t result;
    int total_months, max_day;

    vigil_vm_stack_pop_n(vm, arg_count);

    tm = get_local_tm(ts, &storage);
    if (!tm) return push_i64(vm, ts, error);
    val = *tm;

    total_months = val.tm_mon + n;
    val.tm_year += total_months / 12;
    val.tm_mon = total_months % 12;
    if (val.tm_mon < 0) { val.tm_mon += 12; val.tm_year--; }

    max_day = days_in_month(val.tm_year + 1900, val.tm_mon);
    if (val.tm_mday > max_day) val.tm_mday = max_day;
    val.tm_isdst = -1;

    result = mktime(&val);
    return push_i64(vm, (int64_t)result, error);
}

/* ── time.add_years(ts: i64, n: i32) -> i64 ──────────────────────── */

static vigil_status_t time_add_years(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    int32_t n = get_i32_arg(vm, base, 1);
    struct tm storage, *tm;
    struct tm val;
    time_t result;
    int max_day;

    vigil_vm_stack_pop_n(vm, arg_count);

    tm = get_local_tm(ts, &storage);
    if (!tm) return push_i64(vm, ts, error);
    val = *tm;

    val.tm_year += n;
    max_day = days_in_month(val.tm_year + 1900, val.tm_mon);
    if (val.tm_mday > max_day) val.tm_mday = max_day;
    val.tm_isdst = -1;

    result = mktime(&val);
    return push_i64(vm, (int64_t)result, error);
}

/* ── Timezone conversion functions ───────────────────────────────── */

static vigil_status_t time_to_utc(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, ts - compute_utc_offset_for(ts), error);
}

static vigil_status_t time_from_utc(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, ts + compute_utc_offset_for(ts), error);
}

static vigil_status_t time_format_utc(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    const char *fmt;
    size_t fmt_len;
    struct tm storage, *tm;
    char buf[256], fmt_buf[128];
    size_t len;

    if (!get_string_arg(vm, base, 1, &fmt, &fmt_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    tm = get_utc_tm(ts, &storage);
    if (!tm) return push_string(vm, "", 0, error);

    if (fmt_len >= sizeof(fmt_buf)) fmt_len = sizeof(fmt_buf) - 1;
    memcpy(fmt_buf, fmt, fmt_len);
    fmt_buf[fmt_len] = '\0';

    len = strftime(buf, sizeof(buf), fmt_buf, tm);
    return push_string(vm, buf, len, error);
}

/* ── time.monotonic_ns() -> i64 ──────────────────────────────────── */

static vigil_status_t time_monotonic_ns(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    (void)arg_count;
    /* vigil_platform_monotonic_ns does not exist; use vigil_platform_now_ns as fallback */
    return push_i64(vm, vigil_platform_now_ns(), error);
}

/* ── Convenience formatting ──────────────────────────────────────── */

static vigil_status_t time_iso8601(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    char buf[64];
    size_t len;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    if (!tm) return push_string(vm, "", 0, error);
    len = strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", tm);
    return push_string(vm, buf, len, error);
}

static vigil_status_t time_rfc3339(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    char buf[64];
    int32_t off, off_h, off_m;
    int n;
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    if (!tm) return push_string(vm, "", 0, error);
    n = (int)strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", tm);
    off = compute_utc_offset_for(ts);
    off_h = off / 3600;
    off_m = (off % 3600) / 60;
    if (off_m < 0) off_m = -off_m;
    n += snprintf(buf + n, sizeof(buf) - (size_t)n, "%+03d:%02d", off_h, off_m);
    return push_string(vm, buf, (size_t)n, error);
}

static vigil_status_t time_rfc2822(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    char buf[64];
    int32_t off, off_h, off_m;
    int n;
    static const char *const wday[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char *const mon[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                       "Jul","Aug","Sep","Oct","Nov","Dec"};
    vigil_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    if (!tm) return push_string(vm, "", 0, error);
    off = compute_utc_offset_for(ts);
    off_h = off / 3600;
    off_m = (off % 3600) / 60;
    if (off_m < 0) off_m = -off_m;
    n = snprintf(buf, sizeof(buf), "%s, %02d %s %04d %02d:%02d:%02d %+03d%02d",
                 wday[tm->tm_wday], tm->tm_mday, mon[tm->tm_mon],
                 tm->tm_year + 1900, tm->tm_hour, tm->tm_min, tm->tm_sec,
                 off_h, off_m);
    return push_string(vm, buf, (size_t)n, error);
}

/* ── Millisecond helpers ─────────────────────────────────────────── */

static vigil_status_t time_from_ms(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ms = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, ms / 1000, error);
}

static vigil_status_t time_to_ms(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, ts * 1000, error);
}

/* ── Module definition ───────────────────────────────────────────── */

static const int i64_param[] = {VIGIL_TYPE_I64};
static const int i64_i32_param[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I32};
static const int i64_i64_param[] = {VIGIL_TYPE_I64, VIGIL_TYPE_I64};
static const int i64_str_param[] = {VIGIL_TYPE_I64, VIGIL_TYPE_STRING};
static const int str_str_param[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int date_params[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32,
                                  VIGIL_TYPE_I32, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const char *const time_ms_param_names[] = {"ms"};
static const char *const time_ts_param_names[] = {"ts"};
static const char *const time_ts_fmt_param_names[] = {"ts", "fmt"};
static const char *const time_s_fmt_param_names[] = {"s", "fmt"};
static const char *const time_ts_n_i32_param_names[] = {"ts", "n"};
static const char *const time_ts_n_i64_param_names[] = {"ts", "n"};
static const char *const time_a_b_param_names[] = {"a", "b"};
static const char *const time_date_param_names[] = {"y", "m", "d", "h", "min", "s"};

static const vigil_native_symbol_doc_t vigil_time_module_doc = {
    "Date and time operations.",
    "The time module provides functions for working with Unix timestamps and local time formatting and parsing.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_time_now_doc = {
    "Get current Unix timestamp.",
    "Returns seconds since 1970-01-01 UTC.",
    "i64 ts = time.now()",
};

static const vigil_native_symbol_doc_t vigil_time_now_ms_doc = {
    "Get current time in milliseconds.",
    "Returns milliseconds since the Unix epoch.",
    "i64 ms = time.now_ms()",
};

static const vigil_native_symbol_doc_t vigil_time_now_ns_doc = {
    "Get current time in nanoseconds.",
    "Returns nanoseconds since the Unix epoch.",
    "i64 ns = time.now_ns()",
};

static const vigil_native_symbol_doc_t vigil_time_sleep_doc = {
    "Sleep for milliseconds.",
    "Pauses execution for the specified duration.",
    "time.sleep(i64(1000))",
};

static const vigil_native_symbol_doc_t vigil_time_year_doc = {
    "Get year from timestamp.",
    "Returns the calendar year for the local timestamp.",
    "time.year(time.now())",
};

static const vigil_native_symbol_doc_t vigil_time_month_doc = {
    "Get month from timestamp.",
    "Returns month 1 through 12.",
    "time.month(time.now())",
};

static const vigil_native_symbol_doc_t vigil_time_day_doc = {
    "Get day from timestamp.",
    "Returns day of month 1 through 31.",
    "time.day(time.now())",
};

static const vigil_native_symbol_doc_t vigil_time_hour_doc = {
    "Get hour from timestamp.",
    "Returns hour 0 through 23.",
    "time.hour(time.now())",
};

static const vigil_native_symbol_doc_t vigil_time_minute_doc = {
    "Get minute from timestamp.",
    "Returns minute 0 through 59.",
    "time.minute(time.now())",
};

static const vigil_native_symbol_doc_t vigil_time_second_doc = {
    "Get second from timestamp.",
    "Returns second 0 through 59.",
    "time.second(time.now())",
};

static const vigil_native_symbol_doc_t vigil_time_weekday_doc = {
    "Get day of week.",
    "Returns 0 for Sunday through 6 for Saturday.",
    "time.weekday(time.now())",
};

static const vigil_native_symbol_doc_t vigil_time_yearday_doc = {
    "Get day of year.",
    "Returns day of year 1 through 366.",
    "time.yearday(time.now())",
};

static const vigil_native_symbol_doc_t vigil_time_is_dst_doc = {
    "Check if DST is active.",
    "Returns true if daylight saving time is in effect.",
    "time.is_dst(time.now())",
};

static const vigil_native_symbol_doc_t vigil_time_utc_offset_doc = {
    "Get local UTC offset for a timestamp.",
    "Returns offset from UTC in seconds for the given timestamp.",
    "time.utc_offset(time.now())",
};

static const vigil_native_symbol_doc_t vigil_time_date_doc = {
    "Create timestamp from components.",
    "Returns a Unix timestamp for the given local time.",
    "time.date(2024, 12, 25, 0, 0, 0)",
};

static const vigil_native_symbol_doc_t vigil_time_format_doc = {
    "Format timestamp as string.",
    "Uses strftime format codes.",
    "time.format(time.now(), \"%Y-%m-%d %H:%M:%S\")",
};

static const vigil_native_symbol_doc_t vigil_time_parse_doc = {
    "Parse string to timestamp.",
    "Returns (i64, err). Uses strptime format codes.",
    "i64 ts, err e = time.parse(\"2024-12-25\", \"%Y-%m-%d\")",
};

static const vigil_native_symbol_doc_t vigil_time_add_days_doc = {
    "Add days to timestamp.",
    "Returns a new timestamp offset by whole days.",
    "time.add_days(time.now(), 7)",
};

static const vigil_native_symbol_doc_t vigil_time_add_hours_doc = {
    "Add hours to timestamp.",
    "Returns a new timestamp offset by whole hours.",
    "time.add_hours(time.now(), 24)",
};

static const vigil_native_symbol_doc_t vigil_time_add_minutes_doc = {
    "Add minutes to timestamp.",
    "Returns a new timestamp offset by whole minutes.",
    "time.add_minutes(time.now(), 30)",
};

static const vigil_native_symbol_doc_t vigil_time_add_seconds_doc = {
    "Add seconds to timestamp.",
    "Returns a new timestamp offset by whole seconds.",
    "time.add_seconds(time.now(), i64(3600))",
};

static const vigil_native_symbol_doc_t vigil_time_diff_days_doc = {
    "Get difference in days.",
    "Returns the whole-day difference between two timestamps.",
    "time.diff_days(future, past)",
};

static const vigil_native_symbol_doc_t vigil_time_utc_year_doc = {
    "Get UTC year from timestamp.", "Returns the calendar year in UTC.", "time.utc_year(time.now())",
};
static const vigil_native_symbol_doc_t vigil_time_utc_month_doc = {
    "Get UTC month from timestamp.", "Returns month 1-12 in UTC.", "time.utc_month(time.now())",
};
static const vigil_native_symbol_doc_t vigil_time_utc_day_doc = {
    "Get UTC day from timestamp.", "Returns day of month 1-31 in UTC.", "time.utc_day(time.now())",
};
static const vigil_native_symbol_doc_t vigil_time_utc_hour_doc = {
    "Get UTC hour from timestamp.", "Returns hour 0-23 in UTC.", "time.utc_hour(time.now())",
};
static const vigil_native_symbol_doc_t vigil_time_utc_minute_doc = {
    "Get UTC minute from timestamp.", "Returns minute 0-59 in UTC.", "time.utc_minute(time.now())",
};
static const vigil_native_symbol_doc_t vigil_time_utc_second_doc = {
    "Get UTC second from timestamp.", "Returns second 0-59 in UTC.", "time.utc_second(time.now())",
};
static const vigil_native_symbol_doc_t vigil_time_utc_weekday_doc = {
    "Get UTC day of week.", "Returns 0 for Sunday through 6 for Saturday in UTC.", "time.utc_weekday(time.now())",
};
static const vigil_native_symbol_doc_t vigil_time_utc_yearday_doc = {
    "Get UTC day of year.", "Returns day of year 1-366 in UTC.", "time.utc_yearday(time.now())",
};
static const vigil_native_symbol_doc_t vigil_time_diff_hours_doc = {
    "Get difference in hours.", "Returns the whole-hour difference between two timestamps.", "time.diff_hours(a, b)",
};
static const vigil_native_symbol_doc_t vigil_time_diff_minutes_doc = {
    "Get difference in minutes.", "Returns the whole-minute difference between two timestamps.",
    "time.diff_minutes(a, b)",
};
static const vigil_native_symbol_doc_t vigil_time_diff_seconds_doc = {
    "Get difference in seconds.", "Returns the second difference between two timestamps.", "time.diff_seconds(a, b)",
};
static const vigil_native_symbol_doc_t vigil_time_add_months_doc = {
    "Add months to timestamp.", "Calendar-aware: clamps day to month length.", "time.add_months(time.now(), 3)",
};
static const vigil_native_symbol_doc_t vigil_time_add_years_doc = {
    "Add years to timestamp.", "Calendar-aware: clamps Feb 29 on non-leap years.", "time.add_years(time.now(), 1)",
};
static const vigil_native_symbol_doc_t vigil_time_to_utc_doc = {
    "Convert local timestamp to UTC.", "Subtracts the local UTC offset.", "time.to_utc(time.now())",
};
static const vigil_native_symbol_doc_t vigil_time_from_utc_doc = {
    "Convert UTC timestamp to local.", "Adds the local UTC offset.", "time.from_utc(ts)",
};
static const vigil_native_symbol_doc_t vigil_time_format_utc_doc = {
    "Format timestamp as UTC string.", "Uses gmtime instead of localtime.",
    "time.format_utc(time.now(), \"%Y-%m-%d %H:%M:%S\")",
};
static const vigil_native_symbol_doc_t vigil_time_monotonic_ns_doc = {
    "Get monotonic clock in nanoseconds.", "Returns a monotonic timestamp for measuring elapsed time.",
    "i64 start = time.monotonic_ns()",
};
static const vigil_native_symbol_doc_t vigil_time_iso8601_doc = {
    "Format as ISO 8601.", "Returns \"YYYY-MM-DDTHH:MM:SS\" in local time.", "time.iso8601(time.now())",
};
static const vigil_native_symbol_doc_t vigil_time_rfc3339_doc = {
    "Format as RFC 3339.", "Returns \"YYYY-MM-DDTHH:MM:SS+HH:MM\" with UTC offset.", "time.rfc3339(time.now())",
};
static const vigil_native_symbol_doc_t vigil_time_rfc2822_doc = {
    "Format as RFC 2822.", "Returns \"Day, DD Mon YYYY HH:MM:SS +HHMM\".", "time.rfc2822(time.now())",
};
static const vigil_native_symbol_doc_t vigil_time_from_ms_doc = {
    "Convert milliseconds to seconds.", "Returns ms / 1000.", "time.from_ms(ms)",
};
static const vigil_native_symbol_doc_t vigil_time_to_ms_doc = {
    "Convert seconds to milliseconds.", "Returns ts * 1000.", "time.to_ms(time.now())",
};

static const int i64_err_returns[] = {VIGIL_TYPE_I64, VIGIL_TYPE_ERR};

static const vigil_native_module_function_t time_functions[] = {
    {"now", 3U, time_now, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL, &vigil_time_now_doc},
    {"now_ms", 6U, time_now_ms, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     &vigil_time_now_ms_doc},
    {"now_ns", 6U, time_now_ns, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     &vigil_time_now_ns_doc},
    {"sleep", 5U, time_sleep, 1U, i64_param, VIGIL_TYPE_VOID, 0U, NULL, 0, NULL, NULL, 0U, time_ms_param_names, NULL,
     NULL, &vigil_time_sleep_doc},
    {"year", 4U, time_year, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, time_ts_param_names, NULL, NULL,
     &vigil_time_year_doc},
    {"month", 5U, time_month, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, time_ts_param_names, NULL,
     NULL, &vigil_time_month_doc},
    {"day", 3U, time_day, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, time_ts_param_names, NULL, NULL,
     &vigil_time_day_doc},
    {"hour", 4U, time_hour, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, time_ts_param_names, NULL, NULL,
     &vigil_time_hour_doc},
    {"minute", 6U, time_minute, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, time_ts_param_names, NULL,
     NULL, &vigil_time_minute_doc},
    {"second", 6U, time_second, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, time_ts_param_names, NULL,
     NULL, &vigil_time_second_doc},
    {"weekday", 7U, time_weekday, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, time_ts_param_names, NULL,
     NULL, &vigil_time_weekday_doc},
    {"yearday", 7U, time_yearday, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, time_ts_param_names, NULL,
     NULL, &vigil_time_yearday_doc},
    {"is_dst", 6U, time_is_dst, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U, time_ts_param_names, NULL,
     NULL, &vigil_time_is_dst_doc},
    {"utc_offset", 10U, time_utc_offset, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_param_names, NULL, NULL, &vigil_time_utc_offset_doc},
    {"date", 4U, time_date, 6U, date_params, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, time_date_param_names, NULL,
     NULL, &vigil_time_date_doc},
    {"format", 6U, time_format, 2U, i64_str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_fmt_param_names, NULL, NULL, &vigil_time_format_doc},
    {"parse", 5U, time_parse, 2U, str_str_param, VIGIL_TYPE_I64, 2U, i64_err_returns, 0, NULL, NULL, 0U,
     time_s_fmt_param_names, NULL, NULL, &vigil_time_parse_doc},
    {"add_days", 8U, time_add_days, 2U, i64_i32_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_n_i32_param_names, NULL, NULL, &vigil_time_add_days_doc},
    {"add_hours", 9U, time_add_hours, 2U, i64_i32_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_n_i32_param_names, NULL, NULL, &vigil_time_add_hours_doc},
    {"add_minutes", 11U, time_add_minutes, 2U, i64_i32_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_n_i32_param_names, NULL, NULL, &vigil_time_add_minutes_doc},
    {"add_seconds", 11U, time_add_seconds, 2U, i64_i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_n_i64_param_names, NULL, NULL, &vigil_time_add_seconds_doc},
    {"diff_days", 9U, time_diff_days, 2U, i64_i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     time_a_b_param_names, NULL, NULL, &vigil_time_diff_days_doc},
    {"utc_year", 8U, time_utc_year, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_param_names, NULL, NULL, &vigil_time_utc_year_doc},
    {"utc_month", 9U, time_utc_month, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_param_names, NULL, NULL, &vigil_time_utc_month_doc},
    {"utc_day", 7U, time_utc_day, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_param_names, NULL, NULL, &vigil_time_utc_day_doc},
    {"utc_hour", 8U, time_utc_hour, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_param_names, NULL, NULL, &vigil_time_utc_hour_doc},
    {"utc_minute", 10U, time_utc_minute, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_param_names, NULL, NULL, &vigil_time_utc_minute_doc},
    {"utc_second", 10U, time_utc_second, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_param_names, NULL, NULL, &vigil_time_utc_second_doc},
    {"utc_weekday", 11U, time_utc_weekday, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_param_names, NULL, NULL, &vigil_time_utc_weekday_doc},
    {"utc_yearday", 11U, time_utc_yearday, 1U, i64_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_param_names, NULL, NULL, &vigil_time_utc_yearday_doc},
    {"diff_hours", 10U, time_diff_hours, 2U, i64_i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     time_a_b_param_names, NULL, NULL, &vigil_time_diff_hours_doc},
    {"diff_minutes", 12U, time_diff_minutes, 2U, i64_i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     time_a_b_param_names, NULL, NULL, &vigil_time_diff_minutes_doc},
    {"diff_seconds", 12U, time_diff_seconds, 2U, i64_i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     time_a_b_param_names, NULL, NULL, &vigil_time_diff_seconds_doc},
    {"add_months", 10U, time_add_months, 2U, i64_i32_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_n_i32_param_names, NULL, NULL, &vigil_time_add_months_doc},
    {"add_years", 9U, time_add_years, 2U, i64_i32_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_n_i32_param_names, NULL, NULL, &vigil_time_add_years_doc},
    {"to_utc", 6U, time_to_utc, 1U, i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_param_names, NULL, NULL, &vigil_time_to_utc_doc},
    {"from_utc", 8U, time_from_utc, 1U, i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_param_names, NULL, NULL, &vigil_time_from_utc_doc},
    {"format_utc", 10U, time_format_utc, 2U, i64_str_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_fmt_param_names, NULL, NULL, &vigil_time_format_utc_doc},
    {"monotonic_ns", 12U, time_monotonic_ns, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, &vigil_time_monotonic_ns_doc},
    {"iso8601", 7U, time_iso8601, 1U, i64_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_param_names, NULL, NULL, &vigil_time_iso8601_doc},
    {"rfc3339", 7U, time_rfc3339, 1U, i64_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_param_names, NULL, NULL, &vigil_time_rfc3339_doc},
    {"rfc2822", 7U, time_rfc2822, 1U, i64_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_param_names, NULL, NULL, &vigil_time_rfc2822_doc},
    {"from_ms", 7U, time_from_ms, 1U, i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     time_ms_param_names, NULL, NULL, &vigil_time_from_ms_doc},
    {"to_ms", 5U, time_to_ms, 1U, i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U,
     time_ts_param_names, NULL, NULL, &vigil_time_to_ms_doc},
};

#define TIME_FUNCTION_COUNT (sizeof(time_functions) / sizeof(time_functions[0]))

VIGIL_API const vigil_native_module_t vigil_stdlib_time = {
    "time", 4U, time_functions, TIME_FUNCTION_COUNT, NULL, 0U, &vigil_time_module_doc, NULL, 0U};
