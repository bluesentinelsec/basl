/* sysquery_common.c — Cross-platform sysquery functions. */
#include "sysquery_platform.h"

#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

int sysquery_common_resolve(const char *hostname, char out[][64], int max_count)
{
    int count = 0;
    struct addrinfo hints, *res, *cur;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, NULL, &hints, &res) != 0)
        return 0;

    for (cur = res; cur && count < max_count; cur = cur->ai_next)
    {
        void *addr;
        if (cur->ai_family == AF_INET)
            addr = &((struct sockaddr_in *)cur->ai_addr)->sin_addr;
        else if (cur->ai_family == AF_INET6)
            addr = &((struct sockaddr_in6 *)cur->ai_addr)->sin6_addr;
        else
            continue;

#ifdef _WIN32
        /* Windows inet_ntop needs ws2tcpip.h */
        DWORD len = 64;
        struct sockaddr *sa = cur->ai_addr;
        if (WSAAddressToStringA(sa, (DWORD)cur->ai_addrlen, NULL, out[count], &len) == 0)
            count++;
#else
        if (inet_ntop(cur->ai_family, addr, out[count], 64))
            count++;
#endif
    }
    freeaddrinfo(res);
    return count;
}
