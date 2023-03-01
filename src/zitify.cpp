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

#define  _GNU_SOURCE

#include <sys/socket.h>
#include <uv.h>
#include <string>

#include <ziti/zitilib.h>
#include <ziti/model_collections.h>

#include "std_funcs.h"

#if defined(DEBUG_zitify)
#define LOG(f, ...) fprintf(stderr, "[%d] %s: " f "\n", getpid(), __FUNCTION__, ##__VA_ARGS__)
#else
#define LOG(f,...)
#endif

static const struct stdlib_funcs_s& stdlib = *stdlib_funcs();

static uv_once_t load_once;

static void load_identities() {
    Ziti_lib_init();

    const char *env_str = getenv("ZITI_IDENTITIES");
    if (env_str == nullptr) { return; }

    std::string ids(env_str);
    size_t pos;
    do {
        pos = ids.find(';');
        auto id = ids.substr(0, pos);
        Ziti_load_context(id.c_str());
        ids.erase(0, pos + 1);
    } while (pos != std::string::npos);
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
        lazy_load();
    }
};

static Zitifier loader;

int gethostbyaddr_r(const void *addr, socklen_t len, int type,
        struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop) {
    char b[UV_MAXHOSTNAMESIZE];
    uv_inet_ntop(type, addr, b, sizeof(b));
    LOG("len=%d type=%d: %s ", len, type, b);

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
    LOG("gethostbyaddr()");
    return stdlib.gethostbyaddr_f(addr, len, type);
}

int getaddrinfo(const char *__restrict name,
                const char *__restrict service,
                const struct addrinfo *__restrict hints,
                struct addrinfo **__restrict pai) {
    LOG("resolving %s:%s", name, service);
    int rc = Ziti_resolve(name, service, hints, pai);
    if (rc != 0) {
        rc = stdlib.getaddrinfo_f(name, service, hints, pai);
    }

    return rc;
}

int getnameinfo(const struct sockaddr *addr, socklen_t salen,
                char *host, size_t hostlen,
                char *serv, size_t servlen, int flags) {
    LOG("in getnameinfo");
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
        LOG("fallback getnameinfo", hostname);
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
        LOG("fallback connect: %s", addrbuf);
        return stdlib.connect_f(fd, addr, size);
    }
    port = ntohs(port);

    LOG("connecting fd=%d addr=%s(%s):%d", fd, addrbuf, hostname, port);

    int flags = fcntl(fd, F_GETFL);
    int rc = Ziti_connect_addr(fd, hostname, (unsigned int)(port));
    fcntl(fd, F_SETFL, flags);
    LOG("connected fd=%d rc=%d", fd, rc);

    return rc;
}

int setsockopt(int fd, int level, int optname,
		       const void *optval, socklen_t optlen) {
    if (Ziti_check_socket(fd) == 0) {
        return stdlib.setsockopt_f(fd, level, optname, optval, optlen);
    }
    return 0;
}

//int listen(int fd, int backlog) {
//    return Ziti_listen(fd, backlog);
//}
//
//int accept(int fd, struct sockaddr *addr, socklen_t *socklen) {
//    ziti_socket_t clt = Ziti_accept(fd, NULL, 0);
//    if (socklen)
//        *socklen = 0;
//    fprintf(stderr,"accepted client=%d\n", clt);
//    return clt;
//}

//int bind(int fd, const struct sockaddr *addr, socklen_t len) {
//    std::cerr << "in my bind(" << fd << ")" << std::endl;
//    int type = 0;
//    socklen_t l = sizeof(type);
//    ziti_context ztx = Ziti_load_context("/home/eugene/work/zeds/ek-zeds-host.json");
//    auto service ="super-service ek-test Z29vZ2xlLW9hdXRoMnwxMTM2MjIxOTc4NjgzNDE3NzY1MDg=";
//    int rc = Ziti_bind(fd, ztx, service, nullptr);
//    if (rc != 0) {
//        fprintf(stderr,"bind error(): %d/%s\n", Ziti_last_error(), ziti_errorstr(Ziti_last_error()));
//        if (Ziti_last_error() == EALREADY) {
//            return 0;
//        }
//    }
//    // int rc = getsockopt_f(fd, SOL_SOCKET, SO_DOMAIN, &type, &l);
////    fprintf(stderr,"\nfd = %d, type = %d, rc = %d\n", fd, type, rc);
////
////    if (type == AF_INET)
////        rc = bind_f(fd, addr, len);
////    else
////        rc = 0;
//    fprintf(stderr,"\nbind(): fd = %d, rc = %d\n", fd, rc);
//
//    return rc;
//}
