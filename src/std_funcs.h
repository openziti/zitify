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

#ifndef ZITIFY_STD_FUNCS_H
#define ZITIFY_STD_FUNCS_H

#include <sys/socket.h>

typedef int (*syscall_f_t)(long sysno, ...);
typedef int (*fork_f_t)();
typedef int (*socket_f_t)(int, int, int);
typedef int (*connect_f_t)(int, const struct sockaddr *, socklen_t);
typedef int (*listen_f_t)(int, int);
typedef int (*bind_f_t)(int, const struct sockaddr *, socklen_t);
typedef int (*setsockopt_f_t)(int fd, int level, int optname,
                              const void *optval, socklen_t optlen);


typedef int (*getnameinfo_f_t)(const struct sockaddr *sa, socklen_t salen,
                               char *host, size_t hostlen,
                               char *serv, size_t servlen, int flags);

typedef int (*gethostbyaddr_r_f_t)(const void *addr, socklen_t len, int type, struct hostent *ret, char *buf, size_t buflen,
                                   struct hostent **result, int *h_errnop);

typedef struct hostent *(*gethostbyaddr_f_t)(const void *addr, socklen_t len, int type);

typedef int (*getaddrinfo_f_t)(const char *name, const char *service, const struct addrinfo *req, struct addrinfo **pai);
typedef int (*getsockopt_f_t)(int fd, int level, int optname, void *optval, socklen_t *optlen);
typedef int (*close_f_t)(int fd);

typedef int (*accept_f_t)(int, const struct sockaddr*, socklen_t *);
typedef int (*accept4_f_t)(int, const struct sockaddr*, socklen_t *, int);


#define LIB_FUNCS(XX)   \
    XX(socket)          \
    XX(connect)         \
    XX(listen)          \
    XX(bind)            \
    XX(accept)          \
    XX(accept4)         \
    XX(fork)            \
    XX(getnameinfo)     \
    XX(getaddrinfo)     \
    XX(gethostbyaddr)   \
    XX(gethostbyaddr_r) \
    XX(setsockopt)      \
    XX(close)

struct stdlib_funcs_s {
#define decl_stdlib_f(f) f##_f_t f##_f;
    LIB_FUNCS(decl_stdlib_f)
};

#ifdef __cplusplus
extern "C" {
#endif

extern const struct stdlib_funcs_s *stdlib_funcs();

#ifdef __cplusplus
};
#endif

#endif//ZITIFY_STD_FUNCS_H
