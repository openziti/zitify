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
#include <dlfcn.h>
#include <uv.h>

static uv_once_t init;
static struct stdlib_funcs_s stdlib;

static void do_init() {
#define LOAD_FUNCS(f) stdlib.f##_f = (f##_f_t) dlsym(RTLD_NEXT, #f);
    LIB_FUNCS(LOAD_FUNCS)
}

const struct stdlib_funcs_s *stdlib_funcs() {
    uv_once(&init, do_init);
    return &stdlib;
}