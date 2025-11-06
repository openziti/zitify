// Copyright (c) 2022.  NetFoundry Inc.
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

#include <sys/socket.h>
#include <uv.h>
#include <string>

#include <ziti/zitilib.h>
#include <ziti/ziti_log.h>
#include <ziti/model_collections.h>

#include "std_funcs.h"
#include "zitify.h"


static const struct stdlib_funcs_s& stdlib = *stdlib_funcs();

static uv_once_t load_once;
static ziti_handle_t* ziti_instance;
static void load_identities() {
    Ziti_lib_init();

    const char *env_str = getenv("ZITI_IDENTITIES");
    if (env_str == nullptr) { return; }

    std::string ids(env_str);
    size_t pos;
    pos = ids.find(';');
    auto id = ids.substr(0, pos);
    ziti_instance = new ziti_handle_t();
    Ziti_load_context(ziti_instance, id.c_str());
    ids.erase(0, pos + 1);
}

static void lazy_load() {
    uv_once(&load_once, load_identities);
}


static void zitify() {
    Ziti_lib_init();

}

extern "C" {
uv_thread_t Ziti_lib_thread();
const char *Ziti_lookup(in_addr_t addr);
int Ziti_resolve(const char *host, const char *port, const struct addrinfo *addr, struct addrinfo **addrlist);
}

class Zitifier {
public:
    Zitifier() {
        zitify();
        configure_bindings();
        lazy_load();
    }
};

static Zitifier loader;

ziti_handle_t* get_ziti_context() {
    return ziti_instance;
}

int gethostbyaddr_r(const void *addr, socklen_t len, int type,
        struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop) {
    char b[UV_MAXHOSTNAMESIZE];
    uv_inet_ntop(type, addr, b, sizeof(b));
    ZITI_LOG(DEBUG,"len=%d type=%d: %s ", len, type, b);

//    in_port_t port = 0;
//    in_addr_t in_addr = 0;
//    if (addr->sa_family == AF_INET) {
//        auto addr4 = (sockaddr_in *) addr;
//        in_addr = addr4->sin_addr.s_addr;
//        port = addr4->sin_port;
//    } else if (addr->sa_family == AF_INET6) {
//        auto addr6 = (const sockaddr_in6 *) addr;
//        if (IN6_IS_ADDR_V4MAPPED(&addr6->sin6_addr)) {
//            in_addr = addr6->sin6_addr.s6_addr32[3];
//            port = addr6->sin6_port;
//        }
//    }

    return stdlib.gethostbyaddr_r_f(addr, len, type,
        ret, buf, buflen,
        result, h_errnop);
}

struct hostent *gethostbyaddr(const void *addr,
                              socklen_t len, int type) {
    ZITI_LOG(DEBUG,"gethostbyaddr()");
    return stdlib.gethostbyaddr_f(addr, len, type);
}

int getaddrinfo(const char *__restrict name,
                const char *__restrict service,
                const struct addrinfo *__restrict hints,
                struct addrinfo **__restrict pai) {
    ZITI_LOG(DEBUG,"resolving %s:%s", name, service);
    int rc = Ziti_resolve(name, service, hints, pai);
    if (rc != 0) {
        rc = stdlib.getaddrinfo_f(name, service, hints, pai);
    }

    return rc;
}

int getnameinfo(const struct sockaddr *addr, socklen_t salen,
                char *host, size_t hostlen,
                char *serv, size_t servlen, int flags) {
    ZITI_LOG(DEBUG,"in getnameinfo");
    in_port_t port = 0;
    in_addr_t in_addr = 0;
    if (addr->sa_family == AF_INET) {
        auto addr4 = (sockaddr_in *) addr;
        in_addr = addr4->sin_addr.s_addr;
        port = addr4->sin_port;
    } else if (addr->sa_family == AF_INET6) {
        auto addr6 = (const sockaddr_in6 *) addr;
        if (IN6_IS_ADDR_V4MAPPED(&addr6->sin6_addr)) {
            in_addr = addr6->sin6_addr.s6_addr32[3];
            port = addr6->sin6_port;
        }
    }

    const char* hostname;
    if (in_addr == 0 || (hostname = Ziti_lookup(in_addr)) == nullptr) {
        ZITI_LOG(DEBUG,"fallback getnameinfo", hostname);
        return stdlib.getnameinfo_f(addr, salen, host, hostlen, serv, servlen, flags);
    }

    snprintf(host, hostlen, "%s", hostname);
    snprintf(serv, servlen, "%d", ntohs(port));
    return 0;
}

int connect(int fd, const struct sockaddr *addr, socklen_t size) {
    if (uv_thread_self() == Ziti_lib_thread()) {
        return stdlib.connect_f(fd, addr, size);
    }
    in_port_t port = 0;
    in_addr_t in_addr = 0;
    char addrbuf[UV_MAXHOSTNAMESIZE];
    if (addr->sa_family == AF_INET) {
        auto addr4 = (sockaddr_in *) addr;
        in_addr = addr4->sin_addr.s_addr;
        port = addr4->sin_port;
        uv_inet_ntop(addr->sa_family, &in_addr, addrbuf, sizeof(addrbuf));
    } else if (addr->sa_family == AF_INET6) {
        auto addr6 = (const sockaddr_in6 *) addr;
        if (IN6_IS_ADDR_V4MAPPED(&addr6->sin6_addr)) {
            in_addr = addr6->sin6_addr.s6_addr32[3];
            port = addr6->sin6_port;
        }
        uv_inet_ntop(addr->sa_family, &addr6->sin6_addr, addrbuf, sizeof(addrbuf));
    }

    const char* hostname;
    if (in_addr == 0 || (hostname = Ziti_lookup(in_addr)) == nullptr) {
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

int setsockopt(int fd, int level, int optname,
		       const void *optval, socklen_t optlen) {
    if (Ziti_check_socket(fd) == 0) {
        return stdlib.setsockopt_f(fd, level, optname, optval, optlen);
    }
    return 0;
}
