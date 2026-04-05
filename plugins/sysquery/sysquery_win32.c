/* sysquery_win32.c — Windows platform implementation for sysquery plugin. */
#ifdef _WIN32

#include "sysquery_platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <iphlpapi.h>
#include <lmcons.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>
#include <tlhelp32.h>
#include <windows.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")

void sysquery_plat_sysinfo(sysquery_sysinfo_t *out)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->os_name, sizeof(out->os_name), "Windows");

    DWORD sz = (DWORD)sizeof(out->hostname);
    GetComputerNameA(out->hostname, &sz);

    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture)
    {
    case PROCESSOR_ARCHITECTURE_AMD64:
        snprintf(out->arch, sizeof(out->arch), "x86_64");
        break;
    case PROCESSOR_ARCHITECTURE_ARM64:
        snprintf(out->arch, sizeof(out->arch), "arm64");
        break;
    default:
        snprintf(out->arch, sizeof(out->arch), "x86");
        break;
    }

    /* Build number from registry. */
    HKEY hk;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hk) ==
        ERROR_SUCCESS)
    {
        DWORD bsz = (DWORD)sizeof(out->os_version);
        RegQueryValueExA(hk, "ProductName", NULL, NULL, (BYTE *)out->os_version, &bsz);
        bsz = (DWORD)sizeof(out->build);
        RegQueryValueExA(hk, "CurrentBuildNumber", NULL, NULL, (BYTE *)out->build, &bsz);
        RegCloseKey(hk);
    }

    out->uptime = (int64_t)(GetTickCount64() / 1000);
}

void sysquery_plat_getuid(char *buf, size_t bufsz)
{
    DWORD sz = (DWORD)bufsz;
    if (!GetUserNameA(buf, &sz))
        buf[0] = '\0';
}

void sysquery_plat_getsid(char *buf, size_t bufsz)
{
    buf[0] = '\0';
    char username[UNLEN + 1];
    DWORD ulen = sizeof(username);
    if (!GetUserNameA(username, &ulen))
        return;

    BYTE sid_buf[256];
    DWORD sid_sz = sizeof(sid_buf);
    char domain[256];
    DWORD dom_sz = sizeof(domain);
    SID_NAME_USE use;
    if (LookupAccountNameA(NULL, username, sid_buf, &sid_sz, domain, &dom_sz, &use))
    {
        char *sid_str = NULL;
        if (ConvertSidToStringSidA((PSID)sid_buf, &sid_str))
        {
            snprintf(buf, bufsz, "%s", sid_str);
            LocalFree(sid_str);
        }
    }
}

void sysquery_plat_localtime(char *buf, size_t bufsz)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(buf, bufsz, "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
             st.wSecond);
}

int sysquery_plat_ps(sysquery_process_t *out, int max_count)
{
    int count = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe))
    {
        do
        {
            if (count >= max_count)
                break;
            sysquery_process_t *p = &out[count++];
            memset(p, 0, sizeof(*p));
            p->pid = (int32_t)pe.th32ProcessID;
            p->ppid = (int32_t)pe.th32ParentProcessID;
            snprintf(p->name, sizeof(p->name), "%s", pe.szExeFile);
            snprintf(p->arch, sizeof(p->arch), "%s", sizeof(void *) == 8 ? "x64" : "x86");
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return count;
}

int sysquery_plat_ifconfig(sysquery_interface_t *out, int max_count)
{
    int count = 0;
    ULONG bufsz = 15000;
    IP_ADAPTER_ADDRESSES *addrs = (IP_ADAPTER_ADDRESSES *)malloc(bufsz);
    if (!addrs)
        return 0;
    if (GetAdaptersAddresses(AF_INET, 0, NULL, addrs, &bufsz) != NO_ERROR)
    {
        free(addrs);
        return 0;
    }
    for (IP_ADAPTER_ADDRESSES *a = addrs; a && count < max_count; a = a->Next)
    {
        sysquery_interface_t *iface = &out[count];
        memset(iface, 0, sizeof(*iface));
        WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1, iface->name, sizeof(iface->name), NULL, NULL);
        if (a->FirstUnicastAddress)
        {
            struct sockaddr_in *sa = (struct sockaddr_in *)a->FirstUnicastAddress->Address.lpSockaddr;
            inet_ntop(AF_INET, &sa->sin_addr, iface->ip, sizeof(iface->ip));
        }
        if (a->PhysicalAddressLength == 6)
            snprintf(iface->mac, sizeof(iface->mac), "%02X:%02X:%02X:%02X:%02X:%02X", a->PhysicalAddress[0],
                     a->PhysicalAddress[1], a->PhysicalAddress[2], a->PhysicalAddress[3], a->PhysicalAddress[4],
                     a->PhysicalAddress[5]);
        count++;
    }
    free(addrs);
    return count;
}

int sysquery_plat_netstat(sysquery_connection_t *out, int max_count)
{
    (void)out;
    (void)max_count;
    return 0; /* TODO: GetExtendedTcpTable */
}

int sysquery_plat_arp(sysquery_arp_entry_t *out, int max_count)
{
    (void)out;
    (void)max_count;
    return 0; /* TODO: GetIpNetTable */
}

int sysquery_plat_route(sysquery_route_t *out, int max_count)
{
    (void)out;
    (void)max_count;
    return 0; /* TODO: GetIpForwardTable */
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

#endif /* _WIN32 */
