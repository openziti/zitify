// Copyright (c)  NetFoundry Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//


#include "std_funcs.h"
#include "zitify.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>

#include <ziti/zitilib.h>
#include <ziti/model_collections.h>
#include <ziti/ziti_log.h>


int gethostbyaddr_r(const void *addr, socklen_t len, int type,
        struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop) {
    char b[UV_MAXHOSTNAMESIZE];
    uv_inet_ntop(type, addr, b, sizeof(b));
    ZITI_LOG(DEBUG,"len=%d type=%d: %s ", len, type, b);

    return stdlib_funcs()->gethostbyaddr_r_f(addr, len, type,
        ret, buf, buflen,
        result, h_errnop);
}

struct hostent *gethostbyaddr(const void *addr,
                              socklen_t len, int type) {
    ZITI_LOG(DEBUG,"gethostbyaddr()");
    return stdlib_funcs()->gethostbyaddr_f(addr, len, type);
}

int getaddrinfo(const char *__restrict name,
                const char *__restrict service,
                const struct addrinfo *__restrict hints,
                struct addrinfo **__restrict pai) {
    ZITI_LOG(DEBUG,"resolving %s:%s", name, service);
    int rc = Ziti_resolve(name, service, hints, pai);
    if (rc != 0) {
        rc = stdlib_funcs()->getaddrinfo_f(name, service, hints, pai);
    }

    return rc;
}

int getnameinfo(const struct sockaddr *addr, socklen_t salen,
                char *host, unsigned int hostlen,
                char *serv, unsigned int servlen, int flags) {
    ZITI_LOG(DEBUG,"in getnameinfo");
    in_port_t port = 0;
    in_addr_t in_addr = 0;
    if (addr->sa_family == AF_INET) {
        const struct sockaddr_in* addr4 = (const struct sockaddr_in *) addr;
        in_addr = addr4->sin_addr.s_addr;
        port = addr4->sin_port;
    } else if (addr->sa_family == AF_INET6) {
        const struct sockaddr_in6 *addr6 = (const struct sockaddr_in6 *) addr;
        if (IN6_IS_ADDR_V4MAPPED(&addr6->sin6_addr)) {
            in_addr = addr6->sin6_addr.s6_addr32[3];
            port = addr6->sin6_port;
        }
    }

    const char* hostname = NULL;
    if (in_addr == 0 || (hostname = Ziti_lookup(in_addr)) == NULL) {
        ZITI_LOG(DEBUG,"fallback getnameinfo[%s]", hostname);
        return stdlib_funcs()->getnameinfo_f(addr, salen, host, hostlen, serv, servlen, flags);
    }

    snprintf(host, hostlen, "%s", hostname);
    snprintf(serv, servlen, "%d", ntohs(port));
    return 0;
}
