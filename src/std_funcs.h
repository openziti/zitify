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
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>

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
#define decl_stdlib_f(f) typeof(f) *f##_f;
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
