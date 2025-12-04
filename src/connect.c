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
#include <stdio.h>
#include <stdlib.h>
#include <sys/un.h>

#include <ziti/zitilib.h>
#include <ziti/model_collections.h>
#include <ziti/ziti_log.h>


int connect(int fd, __CONST_SOCKADDR_ARG addr, socklen_t size) {
    const struct stdlib_funcs_s stdlib = *stdlib_funcs();
    if (uv_thread_self() == Ziti_lib_thread()) {
        return stdlib.connect_f(fd, addr, size);
    }
    in_port_t port = 0;
    in_addr_t in_addr = 0;
    char addrbuf[UV_MAXHOSTNAMESIZE];
    if (addr.__sockaddr__->sa_family == AF_INET) {
        in_addr = addr.__sockaddr_in__->sin_addr.s_addr;
        port = addr.__sockaddr_in__->sin_port;
        uv_inet_ntop(AF_INET, &in_addr, addrbuf, sizeof(addrbuf));
    } else if (addr.__sockaddr__->sa_family == AF_INET6) {
        if (IN6_IS_ADDR_V4MAPPED(&addr.__sockaddr_in6__->sin6_addr)) {
            in_addr = addr.__sockaddr_in6__->sin6_addr.s6_addr32[3];
            port = addr.__sockaddr_in6__->sin6_port;
        }
        uv_inet_ntop(AF_INET6, &addr.__sockaddr_in6__->sin6_addr, addrbuf, sizeof(addrbuf));
    }

    const char* hostname;
    // First try Ziti_lookup (for libziti-allocated intercept IPs)
    hostname = Ziti_lookup(in_addr);
    
    // If not found, try fake IP cache (for c-ares compatibility)
    if (hostname == NULL && in_addr != 0) {
        hostname = lookup_hostname_by_fake_ip(in_addr);
        if (hostname != NULL) {
            ZITI_LOG(INFO, "Resolved fake IP %s to hostname %s (from c-ares cache)", addrbuf, hostname);
        }
    }
    
    if (in_addr == 0 || hostname == NULL) {
        ZITI_LOG(DEBUG,"fallback connect: %s", addrbuf);
        return stdlib.connect_f(fd, addr, size);
    }
    port = ntohs(port);

    ZITI_LOG(DEBUG,"connecting fd=%d addr=%s(%s):%d", fd, addrbuf, hostname, port);

    int flags = fcntl(fd, F_GETFL);
    int rc = Ziti_connect_addr(fd, hostname, (unsigned int)(port));
    fcntl(fd, F_SETFL, flags);
    ZITI_LOG(DEBUG,"connected fd=%d rc=%d", fd, rc);

    return rc;
}
