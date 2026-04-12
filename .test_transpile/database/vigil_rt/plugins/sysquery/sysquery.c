/* sysquery.c — Vigil sysquery plugin: system reconnaissance and enumeration.
 *
 * Provides batteries-included capabilities for system inspection:
 * system info, process enumeration, network enumeration.
 */
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"

#include "sysquery_platform.h"

/* ── Stack helpers ───────────────────────────────────────────────── */

static const char *sysquery_arg_str(vigil_vm_t *vm, size_t base, size_t idx, char *buf, size_t bufsz)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    const vigil_object_t *obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(v);
    if (obj && vigil_object_type(obj) == VIGIL_OBJECT_STRING)
    {
        const char *s = vigil_string_object_c_str(obj);
        size_t len = strlen(s);
        if (len >= bufsz)
            len = bufsz - 1;
        memcpy(buf, s, len);
        buf[len] = '\0';
        return buf;
    }
    buf[0] = '\0';
    return buf;
}

static vigil_status_t push_string(vigil_vm_t *vm, const char *s, vigil_error_t *error)
{
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_object_t *obj = NULL;
    vigil_status_t st = vigil_string_object_new_cstr(rt, s, &obj, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_t v;
    vigil_value_init_object(&v, &obj);
    st = vigil_vm_stack_push(vm, &v, error);
    vigil_value_release(&v);
    return st;
}

/* ── sysquery.sysinfo() -> SysInfo ──────────────────────────────────── */

enum
{
    SYSINFO_CLASS = 0U,
    PROCESS_CLASS = 1U,
    INTERFACE_CLASS = 2U,
    CONNECTION_CLASS = 3U,
    ARP_CLASS = 4U,
    ROUTE_CLASS = 5U
};

static vigil_status_t sysquery_fn_sysinfo(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    sysquery_sysinfo_t info;
    sysquery_plat_sysinfo(&info);

    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    /* Build instance with 7 fields. */
    vigil_value_t fields[7];
    vigil_value_init_nil(&fields[0]);
    vigil_value_init_nil(&fields[1]);
    vigil_value_init_nil(&fields[2]);
    vigil_value_init_nil(&fields[3]);
    vigil_value_init_nil(&fields[4]);
    vigil_value_init_nil(&fields[5]);
    vigil_value_init_nil(&fields[6]);

    vigil_object_t *s;
#define SET_STR_FIELD(idx, val)                                                                                        \
    vigil_string_object_new_cstr(rt, val, &s, error);                                                                  \
    vigil_value_init_object(&fields[idx], &s)

    SET_STR_FIELD(0, info.os_name);
    SET_STR_FIELD(1, info.os_version);
    SET_STR_FIELD(2, info.hostname);
    SET_STR_FIELD(3, info.arch);
    SET_STR_FIELD(4, info.build);
    SET_STR_FIELD(5, info.domain);
    vigil_value_init_int(&fields[6], info.uptime);

    vigil_object_t *inst = NULL;
    vigil_status_t st = vigil_instance_object_new(rt, SYSINFO_CLASS, fields, 7U, &inst, error);
    for (int i = 0; i < 7; i++)
        vigil_value_release(&fields[i]);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_t result;
    vigil_value_init_object(&result, &inst);
    st = vigil_vm_stack_push(vm, &result, error);
    vigil_value_release(&result);
    return st;
#undef SET_STR_FIELD
}

/* ── Simple string-returning functions ───────────────────────────── */

static vigil_status_t sysquery_fn_getuid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    char buf[256];
    sysquery_plat_getuid(buf, sizeof(buf));
    return push_string(vm, buf, error);
}

static vigil_status_t sysquery_fn_getsid(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    char buf[256];
    sysquery_plat_getsid(buf, sizeof(buf));
    return push_string(vm, buf, error);
}

static vigil_status_t sysquery_fn_localtime(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    char buf[128];
    sysquery_plat_localtime(buf, sizeof(buf));
    return push_string(vm, buf, error);
}

static vigil_status_t sysquery_fn_getproxy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    char buf[512];
    sysquery_plat_getproxy(buf, sizeof(buf));
    return push_string(vm, buf, error);
}

static vigil_status_t sysquery_fn_resolve(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char hbuf[256];
    const char *hostname = sysquery_arg_str(vm, base, 0, hbuf, sizeof(hbuf));
    vigil_vm_stack_pop_n(vm, arg_count);

    char addrs[16][64];
    int count = sysquery_common_resolve(hostname, addrs, 16);

    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_object_t *arr = NULL;
    vigil_array_object_new(rt, NULL, 0, &arr, error);
    for (int i = 0; i < count; i++)
    {
        vigil_object_t *s = NULL;
        vigil_string_object_new_cstr(rt, addrs[i], &s, error);
        vigil_value_t v;
        vigil_value_init_object(&v, &s);
        vigil_array_object_append(arr, &v, error);
        vigil_value_release(&v);
    }
    vigil_value_t result;
    vigil_value_init_object(&result, &arr);
    vigil_status_t st = vigil_vm_stack_push(vm, &result, error);
    vigil_value_release(&result);
    return st;
}

/* ── sysquery.ps() -> array<Process> ────────────────────────────────── */

static vigil_status_t sysquery_fn_ps(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);

    sysquery_process_t procs[1024];
    int count = sysquery_plat_ps(procs, 1024);

    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_object_t *arr = NULL;
    vigil_array_object_new(rt, NULL, 0, &arr, error);

    for (int i = 0; i < count; i++)
    {
        sysquery_process_t *p = &procs[i];
        vigil_value_t fields[8];
        vigil_value_init_int(&fields[0], p->pid);
        vigil_value_init_int(&fields[1], p->ppid);
        vigil_object_t *s;
#define SF(idx, val)                                                                                                   \
    vigil_string_object_new_cstr(rt, val, &s, error);                                                                  \
    vigil_value_init_object(&fields[idx], &s)
        SF(2, p->name);
        SF(3, p->path);
        SF(4, p->user);
        SF(5, p->arch);
        SF(7, p->args);
#undef SF
        vigil_value_init_int(&fields[6], p->session);

        vigil_object_t *inst = NULL;
        vigil_instance_object_new(rt, PROCESS_CLASS, fields, 8U, &inst, error);
        for (int j = 0; j < 8; j++)
            vigil_value_release(&fields[j]);
        vigil_value_t v;
        vigil_value_init_object(&v, &inst);
        vigil_array_object_append(arr, &v, error);
        vigil_value_release(&v);
    }

    vigil_value_t result;
    vigil_value_init_object(&result, &arr);
    vigil_status_t st = vigil_vm_stack_push(vm, &result, error);
    vigil_value_release(&result);
    return st;
}

/* ── sysquery.pgrep(name) -> array<Process> ─────────────────────────── */

static vigil_status_t sysquery_fn_pgrep(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    char nbuf[256];
    const char *name = sysquery_arg_str(vm, base, 0, nbuf, sizeof(nbuf));
    vigil_vm_stack_pop_n(vm, arg_count);

    sysquery_process_t procs[1024];
    int count = sysquery_plat_ps(procs, 1024);

    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_object_t *arr = NULL;
    vigil_array_object_new(rt, NULL, 0, &arr, error);

    for (int i = 0; i < count; i++)
    {
        if (strstr(procs[i].name, name) == NULL)
            continue;
        sysquery_process_t *p = &procs[i];
        vigil_value_t fields[8];
        vigil_value_init_int(&fields[0], p->pid);
        vigil_value_init_int(&fields[1], p->ppid);
        vigil_object_t *s;
#define SF(idx, val)                                                                                                   \
    vigil_string_object_new_cstr(rt, val, &s, error);                                                                  \
    vigil_value_init_object(&fields[idx], &s)
        SF(2, p->name);
        SF(3, p->path);
        SF(4, p->user);
        SF(5, p->arch);
        SF(7, p->args);
#undef SF
        vigil_value_init_int(&fields[6], p->session);
        vigil_object_t *inst = NULL;
        vigil_instance_object_new(rt, PROCESS_CLASS, fields, 8U, &inst, error);
        for (int j = 0; j < 8; j++)
            vigil_value_release(&fields[j]);
        vigil_value_t v;
        vigil_value_init_object(&v, &inst);
        vigil_array_object_append(arr, &v, error);
        vigil_value_release(&v);
    }

    vigil_value_t result;
    vigil_value_init_object(&result, &arr);
    vigil_status_t st = vigil_vm_stack_push(vm, &result, error);
    vigil_value_release(&result);
    return st;
}

/* ── sysquery.ifconfig() -> array<Interface> ────────────────────────── */

static vigil_status_t sysquery_fn_ifconfig(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    sysquery_interface_t ifaces[64];
    int count = sysquery_plat_ifconfig(ifaces, 64);
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_object_t *arr = NULL;
    vigil_array_object_new(rt, NULL, 0, &arr, error);
    for (int i = 0; i < count; i++)
    {
        vigil_value_t fields[5];
        vigil_object_t *s;
#define SF(idx, val)                                                                                                   \
    vigil_string_object_new_cstr(rt, val, &s, error);                                                                  \
    vigil_value_init_object(&fields[idx], &s)
        SF(0, ifaces[i].name);
        SF(1, ifaces[i].ip);
        SF(2, ifaces[i].netmask);
        SF(3, ifaces[i].mac);
        SF(4, ifaces[i].ip6);
#undef SF
        vigil_object_t *inst = NULL;
        vigil_instance_object_new(rt, INTERFACE_CLASS, fields, 5U, &inst, error);
        for (int j = 0; j < 5; j++)
            vigil_value_release(&fields[j]);
        vigil_value_t v;
        vigil_value_init_object(&v, &inst);
        vigil_array_object_append(arr, &v, error);
        vigil_value_release(&v);
    }
    vigil_value_t result;
    vigil_value_init_object(&result, &arr);
    vigil_status_t st = vigil_vm_stack_push(vm, &result, error);
    vigil_value_release(&result);
    return st;
}

/* ── sysquery.netstat() -> array<Connection> ────────────────────────── */

static vigil_status_t sysquery_fn_netstat(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    sysquery_connection_t conns[512];
    int count = sysquery_plat_netstat(conns, 512);
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_object_t *arr = NULL;
    vigil_array_object_new(rt, NULL, 0, &arr, error);
    for (int i = 0; i < count; i++)
    {
        vigil_value_t fields[7];
        vigil_object_t *s;
#define SF(idx, val)                                                                                                   \
    vigil_string_object_new_cstr(rt, val, &s, error);                                                                  \
    vigil_value_init_object(&fields[idx], &s)
        SF(0, conns[i].proto);
        SF(1, conns[i].local_addr);
        vigil_value_init_int(&fields[2], conns[i].local_port);
        SF(3, conns[i].remote_addr);
        vigil_value_init_int(&fields[4], conns[i].remote_port);
        SF(5, conns[i].state);
        vigil_value_init_int(&fields[6], conns[i].pid);
#undef SF
        vigil_object_t *inst = NULL;
        vigil_instance_object_new(rt, CONNECTION_CLASS, fields, 7U, &inst, error);
        for (int j = 0; j < 7; j++)
            vigil_value_release(&fields[j]);
        vigil_value_t v;
        vigil_value_init_object(&v, &inst);
        vigil_array_object_append(arr, &v, error);
        vigil_value_release(&v);
    }
    vigil_value_t result;
    vigil_value_init_object(&result, &arr);
    vigil_status_t st = vigil_vm_stack_push(vm, &result, error);
    vigil_value_release(&result);
    return st;
}

/* ── sysquery.arp() -> array<ArpEntry> ──────────────────────────────── */

static vigil_status_t sysquery_fn_arp(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    sysquery_arp_entry_t entries[256];
    int count = sysquery_plat_arp(entries, 256);
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_object_t *arr = NULL;
    vigil_array_object_new(rt, NULL, 0, &arr, error);
    for (int i = 0; i < count; i++)
    {
        vigil_value_t fields[3];
        vigil_object_t *s;
#define SF(idx, val)                                                                                                   \
    vigil_string_object_new_cstr(rt, val, &s, error);                                                                  \
    vigil_value_init_object(&fields[idx], &s)
        SF(0, entries[i].ip);
        SF(1, entries[i].mac);
        SF(2, entries[i].iface);
#undef SF
        vigil_object_t *inst = NULL;
        vigil_instance_object_new(rt, ARP_CLASS, fields, 3U, &inst, error);
        for (int j = 0; j < 3; j++)
            vigil_value_release(&fields[j]);
        vigil_value_t v;
        vigil_value_init_object(&v, &inst);
        vigil_array_object_append(arr, &v, error);
        vigil_value_release(&v);
    }
    vigil_value_t result;
    vigil_value_init_object(&result, &arr);
    vigil_status_t st = vigil_vm_stack_push(vm, &result, error);
    vigil_value_release(&result);
    return st;
}

/* ── sysquery.route() -> array<Route> ───────────────────────────────── */

static vigil_status_t sysquery_fn_route(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    sysquery_route_t routes[128];
    int count = sysquery_plat_route(routes, 128);
    vigil_runtime_t *rt = vigil_vm_runtime(vm);
    vigil_object_t *arr = NULL;
    vigil_array_object_new(rt, NULL, 0, &arr, error);
    for (int i = 0; i < count; i++)
    {
        vigil_value_t fields[5];
        vigil_object_t *s;
#define SF(idx, val)                                                                                                   \
    vigil_string_object_new_cstr(rt, val, &s, error);                                                                  \
    vigil_value_init_object(&fields[idx], &s)
        SF(0, routes[i].destination);
        SF(1, routes[i].gateway);
        SF(2, routes[i].netmask);
        SF(3, routes[i].iface);
        vigil_value_init_int(&fields[4], routes[i].metric);
#undef SF
        vigil_object_t *inst = NULL;
        vigil_instance_object_new(rt, ROUTE_CLASS, fields, 5U, &inst, error);
        for (int j = 0; j < 5; j++)
            vigil_value_release(&fields[j]);
        vigil_value_t v;
        vigil_value_init_object(&v, &inst);
        vigil_array_object_append(arr, &v, error);
        vigil_value_release(&v);
    }
    vigil_value_t result;
    vigil_value_init_object(&result, &arr);
    vigil_status_t st = vigil_vm_stack_push(vm, &result, error);
    vigil_value_release(&result);
    return st;
}

/* ── Parameter / return types ────────────────────────────────────── */

static const int p_str[] = {VIGIL_TYPE_STRING};
static const int rt_obj[] = {VIGIL_TYPE_OBJECT};
static const int rt_str[] = {VIGIL_TYPE_STRING};

/* ── Class field macros ──────────────────────────────────────────── */

/* clang-format off */
#define RF(n, nl, t) {n, nl, t, 0, NULL, 0U, 0, NULL, NULL}

/* ── SysInfo class ───────────────────────────────────────────────── */

static const vigil_native_class_field_t sysinfo_fields[] = {
    RF("os_name", 7U, VIGIL_TYPE_STRING), RF("os_version", 10U, VIGIL_TYPE_STRING),
    RF("hostname", 8U, VIGIL_TYPE_STRING), RF("arch", 4U, VIGIL_TYPE_STRING),
    RF("build", 5U, VIGIL_TYPE_STRING), RF("domain", 6U, VIGIL_TYPE_STRING),
    RF("uptime", 6U, VIGIL_TYPE_I64),
};

/* ── Process class ───────────────────────────────────────────────── */

static const vigil_native_class_field_t process_fields[] = {
    RF("pid", 3U, VIGIL_TYPE_I32), RF("ppid", 4U, VIGIL_TYPE_I32),
    RF("name", 4U, VIGIL_TYPE_STRING), RF("path", 4U, VIGIL_TYPE_STRING),
    RF("user", 4U, VIGIL_TYPE_STRING), RF("arch", 4U, VIGIL_TYPE_STRING),
    RF("session", 7U, VIGIL_TYPE_I32), RF("args", 4U, VIGIL_TYPE_STRING),
};

/* ── Interface class ─────────────────────────────────────────────── */

static const vigil_native_class_field_t interface_fields[] = {
    RF("name", 4U, VIGIL_TYPE_STRING), RF("ip", 2U, VIGIL_TYPE_STRING),
    RF("netmask", 7U, VIGIL_TYPE_STRING), RF("mac", 3U, VIGIL_TYPE_STRING),
    RF("ip6", 3U, VIGIL_TYPE_STRING),
};

/* ── Connection class ────────────────────────────────────────────── */

static const vigil_native_class_field_t connection_fields[] = {
    RF("proto", 5U, VIGIL_TYPE_STRING), RF("local_addr", 10U, VIGIL_TYPE_STRING),
    RF("local_port", 10U, VIGIL_TYPE_I32), RF("remote_addr", 11U, VIGIL_TYPE_STRING),
    RF("remote_port", 11U, VIGIL_TYPE_I32), RF("state", 5U, VIGIL_TYPE_STRING),
    RF("pid", 3U, VIGIL_TYPE_I32),
};

/* ── ArpEntry class ──────────────────────────────────────────────── */

static const vigil_native_class_field_t arp_fields[] = {
    RF("ip", 2U, VIGIL_TYPE_STRING), RF("mac", 3U, VIGIL_TYPE_STRING),
    RF("iface", 5U, VIGIL_TYPE_STRING),
};

/* ── Route class ─────────────────────────────────────────────────── */

static const vigil_native_class_field_t route_fields[] = {
    RF("destination", 11U, VIGIL_TYPE_STRING), RF("gateway", 7U, VIGIL_TYPE_STRING),
    RF("netmask", 7U, VIGIL_TYPE_STRING), RF("iface", 5U, VIGIL_TYPE_STRING),
    RF("metric", 6U, VIGIL_TYPE_I32),
};

/* ── Class table ─────────────────────────────────────────────────── */

static const vigil_native_class_t sysquery_classes[] = {
    {"SysInfo",    7U,  sysinfo_fields,    7U, NULL, 0, NULL, NULL},
    {"Process",    7U,  process_fields,    8U, NULL, 0, NULL, NULL},
    {"Interface",  9U,  interface_fields,  5U, NULL, 0, NULL, NULL},
    {"Connection", 10U, connection_fields, 7U, NULL, 0, NULL, NULL},
    {"ArpEntry",   8U,  arp_fields,        3U, NULL, 0, NULL, NULL},
    {"Route",      5U,  route_fields,      5U, NULL, 0, NULL, NULL},
};
/* clang-format on */

#define SYSQUERY_CLASS_COUNT (sizeof(sysquery_classes) / sizeof(sysquery_classes[0]))

/* ── Module functions ────────────────────────────────────────────── */

/* clang-format off */
#define SYSQUERY_FN(n, nl, fn, pc, pt, rt, rc, rts) \
    {n, nl, fn, pc, pt, rt, rc, rts, 0, NULL, NULL, 0U, NULL, NULL, NULL, NULL}
/* clang-format on */

static const vigil_native_module_function_t sysquery_functions[] = {
    SYSQUERY_FN("sysinfo", 7U, sysquery_fn_sysinfo, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, rt_obj),
    SYSQUERY_FN("getuid", 6U, sysquery_fn_getuid, 0U, NULL, VIGIL_TYPE_STRING, 1U, rt_str),
    SYSQUERY_FN("getsid", 6U, sysquery_fn_getsid, 0U, NULL, VIGIL_TYPE_STRING, 1U, rt_str),
    SYSQUERY_FN("localtime", 9U, sysquery_fn_localtime, 0U, NULL, VIGIL_TYPE_STRING, 1U, rt_str),
    SYSQUERY_FN("getproxy", 8U, sysquery_fn_getproxy, 0U, NULL, VIGIL_TYPE_STRING, 1U, rt_str),
    SYSQUERY_FN("resolve", 7U, sysquery_fn_resolve, 1U, p_str, VIGIL_TYPE_OBJECT, 1U, rt_obj),
    SYSQUERY_FN("ps", 2U, sysquery_fn_ps, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, rt_obj),
    SYSQUERY_FN("pgrep", 5U, sysquery_fn_pgrep, 1U, p_str, VIGIL_TYPE_OBJECT, 1U, rt_obj),
    SYSQUERY_FN("ifconfig", 8U, sysquery_fn_ifconfig, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, rt_obj),
    SYSQUERY_FN("ipconfig", 8U, sysquery_fn_ifconfig, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, rt_obj),
    SYSQUERY_FN("netstat", 7U, sysquery_fn_netstat, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, rt_obj),
    SYSQUERY_FN("arp", 3U, sysquery_fn_arp, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, rt_obj),
    SYSQUERY_FN("route", 5U, sysquery_fn_route, 0U, NULL, VIGIL_TYPE_OBJECT, 1U, rt_obj),
};

#define SYSQUERY_FUNCTION_COUNT (sizeof(sysquery_functions) / sizeof(sysquery_functions[0]))

/* ── Module doc ──────────────────────────────────────────────────── */

static const vigil_native_symbol_doc_t sysquery_module_doc = {
    "OS metadata queries and system enumeration.",
    "Provides batteries-included capabilities for system inspection: "
    "system info, process enumeration, network interfaces, connections, "
    "ARP cache, routing table, DNS resolution, and proxy detection.",
    NULL,
};

/* ── Module export ───────────────────────────────────────────────── */

VIGIL_API const vigil_native_module_t vigil_plugin_sysquery = {
    "sysquery",
    8U,
    sysquery_functions,
    SYSQUERY_FUNCTION_COUNT,
    sysquery_classes,
    SYSQUERY_CLASS_COUNT,
    &sysquery_module_doc,
    NULL,
    0U,
};
