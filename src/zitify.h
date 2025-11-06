
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

#ifndef ZITIFY_ZITIFY_H
#define ZITIFY_ZITIFY_H

#include <ziti/zitilib.h>
#include <uv.h>

#ifdef __cplusplus
extern "C" {
#endif

ziti_handle_t get_ziti_context();

void configure_bindings();

extern uv_thread_t Ziti_lib_thread();
extern const char *Ziti_lookup(in_addr_t addr);
extern int Ziti_resolve(const char *host, const char *port, const struct addrinfo *addr, struct addrinfo **addrlist);


#ifdef __cplusplus
};
#endif

#endif//ZITIFY_ZITIFY_H
