/* sysquery_stub.c — Stub sysquery backend for platforms without native support.
 *
 * Used on Android, iOS, Emscripten, and other non-desktop targets.
 * All functions return empty/zeroed results. Future work can replace
 * individual stubs with real implementations where the platform allows.
 */

#include "sysquery_platform.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void sysquery_plat_sysinfo(sysquery_sysinfo_t *out)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->os_name, sizeof(out->os_name), "unknown");
    snprintf(out->arch, sizeof(out->arch), "unknown");
}

void sysquery_plat_getuid(char *buf, size_t bufsz)
{
    if (bufsz > 0)
        snprintf(buf, bufsz, "unknown");
}

void sysquery_plat_getsid(char *buf, size_t bufsz)
{
    if (bufsz > 0)
        buf[0] = '\0';
}

void sysquery_plat_localtime(char *buf, size_t bufsz)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t)
        strftime(buf, bufsz, "%Y-%m-%d %H:%M:%S", t);
    else if (bufsz > 0)
        buf[0] = '\0';
}

int sysquery_plat_ps(sysquery_process_t *out, int max_count)
{
    (void)out;
    (void)max_count;
    return 0;
}

int sysquery_plat_ifconfig(sysquery_interface_t *out, int max_count)
{
    (void)out;
    (void)max_count;
    return 0;
}

int sysquery_plat_netstat(sysquery_connection_t *out, int max_count)
{
    (void)out;
    (void)max_count;
    return 0;
}

int sysquery_plat_arp(sysquery_arp_entry_t *out, int max_count)
{
    (void)out;
    (void)max_count;
    return 0;
}

int sysquery_plat_route(sysquery_route_t *out, int max_count)
{
    (void)out;
    (void)max_count;
    return 0;
}

void sysquery_plat_getproxy(char *buf, size_t bufsz)
{
    if (bufsz > 0)
        buf[0] = '\0';
}
