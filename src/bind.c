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

struct binding_s {
    char *service;
    char *terminator;
};

static model_map bindings;

int listen(int fd, int backlog) {
    ZITI_LOG(DEBUG, "in listen(%d,%d)", fd, backlog);

    int rc = Ziti_listen(fd, backlog);
    if (rc != 0) {
        rc = stdlib_funcs()->listen_f(fd, backlog);
    }
    return rc;
}

int accept(int fd, struct sockaddr *addr, socklen_t *socklen) {
    ZITI_LOG(DEBUG, "in accept(%d,...)", fd);

    char caller[128] = "";
    ziti_socket_t clt = Ziti_accept(fd, caller, sizeof(caller));

    if (clt == -1) {
        return stdlib_funcs()->accept_f(fd, addr, socklen);
    }

    if (socklen && addr) {
        struct sockaddr_un *zaddr = addr;
        addr->sa_family = AF_UNIX;
        snprintf(zaddr->sun_path, sizeof(zaddr->sun_path), "ziti:%s", caller);
        *socklen = sizeof(struct sockaddr_un);
    }
    ZITI_LOG(DEBUG,"accepted client=%d(%s)", clt, caller);
    return clt;
}

int accept4(int fd, struct sockaddr *addr, socklen_t *socklen, int flags) {
    ZITI_LOG(DEBUG, "in accept4(%d,...)", fd);
    char caller[128];
    ziti_socket_t clt = Ziti_accept(fd, caller, sizeof(caller));
    if (clt == -1) {
        return stdlib_funcs()->accept4_f(fd, addr, socklen, flags);
    }

    if (socklen && addr) {
        struct sockaddr_un *zaddr = addr;
        addr->sa_family = AF_UNIX;
        snprintf(zaddr->sun_path, sizeof(zaddr->sun_path), "ziti:%s", caller);
        *socklen = sizeof(struct sockaddr_un);
    }
    ZITI_LOG(DEBUG,"accepted client=%d", clt);
    return clt;
}


int bind(int fd, const struct sockaddr *addr, socklen_t len) {
    ZITI_LOG(DEBUG, "in bind(%d,...)", fd);

    uint16_t port16;
    switch (addr->sa_family) {
        case AF_INET:
            port16 = ((const struct sockaddr_in*)addr)->sin_port;
            break;
        case AF_INET6:
            port16 = ((const struct sockaddr_in6*)addr)->sin6_port;
            break;
        default:
            return EINVAL;
    }
    long port = ntohs(port16);
    ZITI_LOG(DEBUG, "looking up binding for port[%ld]", port);

    struct binding_s *b = model_map_getl(&bindings, port);
    if (b != NULL) {
        ZITI_LOG(DEBUG, "found binding[%s@%s] for port[%ld]", b->terminator, b->service, port);
        ziti_handle_t* ztx = get_ziti_context();
        int rc = Ziti_bind(fd, *ztx, b->service, b->terminator);
        if (rc != 0) {
            ZITI_LOG(WARN, "bind error(): %d/%s", Ziti_last_error(), ziti_errorstr(Ziti_last_error()));
            if (Ziti_last_error() == EALREADY) {
                return 0;
            }
        }
        return rc;
    } else {
        return stdlib_funcs()->bind_f(fd, addr, len);
    }
}

void configure_bindings() {
    const char *env_str = getenv("ZITI_BINDINGS");
    if (env_str == NULL) return;

    char *p = strdup(env_str);
    char *bind;
    char *end;
    while((bind = strtok_r(p, ";", &p)) != NULL) {
        char *col = strchr(bind, ':');
        if (!col) {
            ZITI_LOG(ERROR, "invalid binding specification '%s', expecting 'port:[terminator@]service'", bind);
            continue;
        }

        unsigned long port = strtoul(bind, &end, 10);
        if (end != col) {
            ZITI_LOG(ERROR, "invalid binding specification '%s', expecting 'port:[terminator@]service'", bind);
            continue;
        }

        if (port > UINT16_MAX) {
            ZITI_LOG(ERROR, "invalid port specification '%s', expecting 'port:[terminator@]service'", bind);
            continue;
        }

        struct binding_s *b = calloc(1, sizeof(*b));
        char *at = strchr(col, '@');
        if (at) {
            b->service = strdup(at + 1);
            b->terminator = strndup(col + 1, at - col);
        } else {
            b->service = strdup(col + 1);
        }
        ZITI_LOG(INFO, "added binding port[%ld] => %s@%s", port, b->terminator, b->service);
        model_map_setl(&bindings, port, b);
    }
}