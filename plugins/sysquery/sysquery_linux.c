/* sysquery_linux.c — Linux platform implementation for sysquery plugin.
 * Uses /proc filesystem and POSIX APIs.
 */
#ifdef __linux__

#include "sysquery_platform.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

void sysquery_plat_sysinfo(sysquery_sysinfo_t *out)
{
    memset(out, 0, sizeof(*out));
    struct utsname u;
    if (uname(&u) == 0)
    {
        snprintf(out->os_name, sizeof(out->os_name), "%.63s", u.sysname);
        snprintf(out->arch, sizeof(out->arch), "%.31s", u.machine);
        snprintf(out->build, sizeof(out->build), "%.127s", u.release);
        snprintf(out->hostname, sizeof(out->hostname), "%.127s", u.nodename);
    }
    /* Try /etc/os-release for pretty name. */
    FILE *f = fopen("/etc/os-release", "r");
    if (f)
    {
        char line[256];
        while (fgets(line, sizeof(line), f))
        {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0)
            {
                char *val = line + 12;
                if (*val == '"')
                    val++;
                size_t len = strlen(val);
                while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '"'))
                    len--;
                if (len >= sizeof(out->os_version))
                    len = sizeof(out->os_version) - 1;
                memcpy(out->os_version, val, len);
                out->os_version[len] = '\0';
                break;
            }
        }
        fclose(f);
    }
    struct sysinfo si;
    if (sysinfo(&si) == 0)
        out->uptime = (int64_t)si.uptime;
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
    /* No SID concept on Linux. */
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
    DIR *proc = opendir("/proc");
    if (!proc)
        return 0;
    struct dirent *ent;
    while ((ent = readdir(proc)) != NULL && count < max_count)
    {
        int pid = atoi(ent->d_name);
        if (pid <= 0)
            continue;
        sysquery_process_t *p = &out[count];
        memset(p, 0, sizeof(*p));
        p->pid = pid;

        /* Read /proc/PID/stat for ppid, name. */
        char path[128];
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        FILE *f = fopen(path, "r");
        if (f)
        {
            int ppid = 0;
            int session = 0;
            char comm[256] = {0};
            char state;
            if (fscanf(f, "%*d (%255[^)]) %c %d %*d %d", comm, &state, &ppid, &session) >= 3)
            {
                p->ppid = ppid;
                p->session = session;
                snprintf(p->name, sizeof(p->name), "%s", comm);
            }
            fclose(f);
        }

        /* Read /proc/PID/exe for path. */
        snprintf(path, sizeof(path), "/proc/%d/exe", pid);
        ssize_t len = readlink(path, p->path, sizeof(p->path) - 1);
        if (len > 0)
            p->path[len] = '\0';

        /* Read /proc/PID/cmdline for args. */
        snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        f = fopen(path, "r");
        if (f)
        {
            size_t n = fread(p->args, 1, sizeof(p->args) - 1, f);
            for (size_t i = 0; i < n; i++)
                if (p->args[i] == '\0')
                    p->args[i] = ' ';
            if (n > 0)
                p->args[n - 1] = '\0';
            fclose(f);
        }

        /* Read /proc/PID/status for user. */
        snprintf(path, sizeof(path), "/proc/%d/status", pid);
        f = fopen(path, "r");
        if (f)
        {
            char line[256];
            while (fgets(line, sizeof(line), f))
            {
                unsigned uid_val;
                if (sscanf(line, "Uid:\t%u", &uid_val) == 1)
                {
                    struct passwd *pw = getpwuid(uid_val);
                    if (pw)
                        snprintf(p->user, sizeof(p->user), "%s", pw->pw_name);
                    else
                        snprintf(p->user, sizeof(p->user), "%u", uid_val);
                    break;
                }
            }
            fclose(f);
        }

        snprintf(p->arch, sizeof(p->arch), "%s", sizeof(void *) == 8 ? "x64" : "x86");
        count++;
    }
    closedir(proc);
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
        sysquery_interface_t *iface = &out[count];
        memset(iface, 0, sizeof(*iface));
        snprintf(iface->name, sizeof(iface->name), "%s", cur->ifa_name);
        struct sockaddr_in *sa = (struct sockaddr_in *)cur->ifa_addr;
        inet_ntop(AF_INET, &sa->sin_addr, iface->ip, sizeof(iface->ip));
        if (cur->ifa_netmask)
        {
            struct sockaddr_in *nm = (struct sockaddr_in *)cur->ifa_netmask;
            inet_ntop(AF_INET, &nm->sin_addr, iface->netmask, sizeof(iface->netmask));
        }
        count++;
    }
    freeifaddrs(ifa);
    return count;
}

int sysquery_plat_netstat(sysquery_connection_t *out, int max_count)
{
    int count = 0;
    static const char *tcp_states[] = {"",          "ESTABLISHED", "SYN_SENT",   "SYN_RECV", "FIN_WAIT1", "FIN_WAIT2",
                                       "TIME_WAIT", "CLOSE",       "CLOSE_WAIT", "LAST_ACK", "LISTEN",    "CLOSING"};
    FILE *f = fopen("/proc/net/tcp", "r");
    if (f)
    {
        char line[512];
        if (fgets(line, sizeof(line), f))
        { /* skip header */
        }
        while (fgets(line, sizeof(line), f) && count < max_count)
        {
            unsigned la, lp, ra, rp, st;
            int uid;
            if (sscanf(line, " %*d: %X:%X %X:%X %X %*X:%*X %*X:%*X %*X %d", &la, &lp, &ra, &rp, &st, &uid) >= 5)
            {
                sysquery_connection_t *c = &out[count++];
                memset(c, 0, sizeof(*c));
                snprintf(c->proto, sizeof(c->proto), "tcp");
                struct in_addr a;
                a.s_addr = la;
                inet_ntop(AF_INET, &a, c->local_addr, sizeof(c->local_addr));
                c->local_port = (int32_t)lp;
                a.s_addr = ra;
                inet_ntop(AF_INET, &a, c->remote_addr, sizeof(c->remote_addr));
                c->remote_port = (int32_t)rp;
                if (st < sizeof(tcp_states) / sizeof(tcp_states[0]))
                    snprintf(c->state, sizeof(c->state), "%s", tcp_states[st]);
                c->pid = -1;
            }
        }
        fclose(f);
    }
    return count;
}

int sysquery_plat_arp(sysquery_arp_entry_t *out, int max_count)
{
    int count = 0;
    FILE *f = fopen("/proc/net/arp", "r");
    if (!f)
        return 0;
    char line[256];
    if (fgets(line, sizeof(line), f))
    { /* skip header */
    }
    while (fgets(line, sizeof(line), f) && count < max_count)
    {
        sysquery_arp_entry_t *e = &out[count];
        memset(e, 0, sizeof(*e));
        if (sscanf(line, "%63s %*s %*s %23s %*s %63s", e->ip, e->mac, e->iface) >= 2)
            count++;
    }
    fclose(f);
    return count;
}

int sysquery_plat_route(sysquery_route_t *out, int max_count)
{
    int count = 0;
    FILE *f = fopen("/proc/net/route", "r");
    if (!f)
        return 0;
    char line[256];
    if (fgets(line, sizeof(line), f))
    { /* skip header */
    }
    while (fgets(line, sizeof(line), f) && count < max_count)
    {
        char iface[64];
        unsigned dest, gw, mask;
        int metric;
        if (sscanf(line, "%63s %X %X %*d %*d %*d %d %X", iface, &dest, &gw, &metric, &mask) >= 4)
        {
            sysquery_route_t *r = &out[count++];
            memset(r, 0, sizeof(*r));
            snprintf(r->iface, sizeof(r->iface), "%s", iface);
            struct in_addr a;
            a.s_addr = dest;
            inet_ntop(AF_INET, &a, r->destination, sizeof(r->destination));
            a.s_addr = gw;
            inet_ntop(AF_INET, &a, r->gateway, sizeof(r->gateway));
            a.s_addr = mask;
            inet_ntop(AF_INET, &a, r->netmask, sizeof(r->netmask));
            r->metric = metric;
        }
    }
    fclose(f);
    return count;
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

#endif /* __linux__ */
