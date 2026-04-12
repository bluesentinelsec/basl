/* sysquery_platform.h — Platform-specific sysquery functions.
 * Each platform file (sysquery_linux.c, sysquery_darwin.c, sysquery_win32.c)
 * implements these functions.
 */
#ifndef VIGIL_SYSQUERY_PLATFORM_H
#define VIGIL_SYSQUERY_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

/* ── Data structures ─────────────────────────────────────────────── */

typedef struct
{
    char os_name[64];
    char os_version[128];
    char hostname[128];
    char arch[32];
    char build[128];
    char domain[128];
    int64_t uptime;
} sysquery_sysinfo_t;

typedef struct
{
    int32_t pid;
    int32_t ppid;
    char name[256];
    char path[512];
    char user[128];
    char arch[16];
    int32_t session;
    char args[1024];
} sysquery_process_t;

typedef struct
{
    char name[64];
    char ip[64];
    char netmask[64];
    char mac[24];
    char ip6[64];
} sysquery_interface_t;

typedef struct
{
    char proto[8];
    char local_addr[64];
    int32_t local_port;
    char remote_addr[64];
    int32_t remote_port;
    char state[24];
    int32_t pid;
} sysquery_connection_t;

typedef struct
{
    char ip[64];
    char mac[24];
    char iface[64];
} sysquery_arp_entry_t;

typedef struct
{
    char destination[64];
    char gateway[64];
    char netmask[64];
    char iface[64];
    int32_t metric;
} sysquery_route_t;

/* ── Platform functions ──────────────────────────────────────────── */

/* System info */
void sysquery_plat_sysinfo(sysquery_sysinfo_t *out);
void sysquery_plat_getuid(char *buf, size_t bufsz);
void sysquery_plat_getsid(char *buf, size_t bufsz);
void sysquery_plat_localtime(char *buf, size_t bufsz);

/* Process enumeration */
int sysquery_plat_ps(sysquery_process_t *out, int max_count);

/* Network enumeration */
int sysquery_plat_ifconfig(sysquery_interface_t *out, int max_count);
int sysquery_plat_netstat(sysquery_connection_t *out, int max_count);
int sysquery_plat_arp(sysquery_arp_entry_t *out, int max_count);
int sysquery_plat_route(sysquery_route_t *out, int max_count);
void sysquery_plat_getproxy(char *buf, size_t bufsz);

/* Common (cross-platform via getaddrinfo) */
int sysquery_common_resolve(const char *hostname, char out[][64], int max_count);

#endif /* VIGIL_SYSQUERY_PLATFORM_H */
