/* sysquery_darwin.c — macOS platform implementation for sysquery plugin. */
#ifdef __APPLE__

#include "sysquery_platform.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

void sysquery_plat_sysinfo(sysquery_sysinfo_t *out)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->os_name, sizeof(out->os_name), "macOS");
    struct utsname u;
    if (uname(&u) == 0)
    {
        snprintf(out->arch, sizeof(out->arch), "%s", u.machine);
        snprintf(out->build, sizeof(out->build), "%s", u.release);
        snprintf(out->hostname, sizeof(out->hostname), "%s", u.nodename);
    }
    /* Get macOS version via sysctl. */
    char ver[64] = {0};
    size_t len = sizeof(ver);
    if (sysctlbyname("kern.osproductversion", ver, &len, NULL, 0) == 0)
        snprintf(out->os_version, sizeof(out->os_version), "%s", ver);
    /* Uptime. */
    struct timeval boottime;
    len = sizeof(boottime);
    int mib[2] = {CTL_KERN, KERN_BOOTTIME};
    if (sysctl(mib, 2, &boottime, &len, NULL, 0) == 0)
        out->uptime = (int64_t)(time(NULL) - boottime.tv_sec);
}

void sysquery_plat_getuid(char *buf, size_t bufsz)
{
    struct passwd *pw = getpwuid(getuid());
    if (pw)
        snprintf(buf, bufsz, "%s", pw->pw_name);
    else
        snprintf(buf, bufsz, "uid=%d", (int)getuid());
}

void sysquery_plat_getsid(char *buf, size_t bufsz)
{
    buf[0] = '\0';
    (void)bufsz;
}

void sysquery_plat_localtime(char *buf, size_t bufsz)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, bufsz, "%Y-%m-%d %H:%M:%S %Z", &tm);
}

int sysquery_plat_ps(sysquery_process_t *out, int max_count)
{
    int count = 0;
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
    size_t len = 0;
    if (sysctl(mib, 4, NULL, &len, NULL, 0) != 0)
        return 0;
    struct kinfo_proc *procs = malloc(len);
    if (!procs)
        return 0;
    if (sysctl(mib, 4, procs, &len, NULL, 0) != 0)
    {
        free(procs);
        return 0;
    }
    int nprocs = (int)(len / sizeof(struct kinfo_proc));
    for (int i = 0; i < nprocs && count < max_count; i++)
    {
        sysquery_process_t *p = &out[count++];
        memset(p, 0, sizeof(*p));
        p->pid = procs[i].kp_proc.p_pid;
        p->ppid = procs[i].kp_eproc.e_ppid;
        snprintf(p->name, sizeof(p->name), "%s", procs[i].kp_proc.p_comm);
        struct passwd *pw = getpwuid(procs[i].kp_eproc.e_ucred.cr_uid);
        if (pw)
            snprintf(p->user, sizeof(p->user), "%s", pw->pw_name);
        snprintf(p->arch, sizeof(p->arch), "%s", sizeof(void *) == 8 ? "x64" : "arm64");
    }
    free(procs);
    return count;
}

int sysquery_plat_ifconfig(sysquery_interface_t *out, int max_count)
{
    int count = 0;
    struct ifaddrs *ifa, *cur;
    if (getifaddrs(&ifa) != 0)
        return 0;
    for (cur = ifa; cur && count < max_count; cur = cur->ifa_next)
    {
        if (!cur->ifa_addr || cur->ifa_addr->sa_family != AF_INET)
            continue;
        sysquery_interface_t *iface = &out[count++];
        memset(iface, 0, sizeof(*iface));
        snprintf(iface->name, sizeof(iface->name), "%s", cur->ifa_name);
        struct sockaddr_in *sa = (struct sockaddr_in *)cur->ifa_addr;
        inet_ntop(AF_INET, &sa->sin_addr, iface->ip, sizeof(iface->ip));
        if (cur->ifa_netmask)
        {
            struct sockaddr_in *nm = (struct sockaddr_in *)cur->ifa_netmask;
            inet_ntop(AF_INET, &nm->sin_addr, iface->netmask, sizeof(iface->netmask));
        }
    }
    freeifaddrs(ifa);
    return count;
}

int sysquery_plat_netstat(sysquery_connection_t *out, int max_count)
{
    (void)out;
    (void)max_count;
    return 0; /* TODO: sysctl net.inet.tcp.pcblist */
}

int sysquery_plat_arp(sysquery_arp_entry_t *out, int max_count)
{
    (void)out;
    (void)max_count;
    return 0; /* TODO: sysctl net.inet.arp */
}

int sysquery_plat_route(sysquery_route_t *out, int max_count)
{
    (void)out;
    (void)max_count;
    return 0; /* TODO: sysctl net.route */
}

void sysquery_plat_getproxy(char *buf, size_t bufsz)
{
    const char *proxy = getenv("http_proxy");
    if (!proxy)
        proxy = getenv("HTTP_PROXY");
    if (proxy)
        snprintf(buf, bufsz, "%s", proxy);
    else
        buf[0] = '\0';
}

#endif /* __APPLE__ */
