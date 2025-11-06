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

#include <uv.h>
#include <string>

#include <ziti/zitilib.h>
#include <ziti/ziti_log.h>
#include <ziti/model_collections.h>

#include "std_funcs.h"
#include "zitify.h"


static uv_once_t load_once;
static ziti_handle_t ziti_instance;
static void load_identities() {
    Ziti_lib_init();

    const char *env_str = getenv("ZITI_IDENTITIES");
    if (env_str == nullptr) { return; }

    std::string ids(env_str);
    size_t pos = ids.find(';');
    auto id = ids.substr(0, pos);
    int error = Ziti_load_context(&ziti_instance, id.c_str());
    if (error != ZITI_OK) {
        ZITI_LOG(ERROR, "failed to load identity '%s': %s", id.c_str(), ziti_errorstr(error));
    } else {
        ZITI_LOG(INFO, "loaded identity '%s'", id.c_str());
    }
    ids.erase(0, pos + 1);
}

static void lazy_load() {
    uv_once(&load_once, load_identities);
}


static void zitify() {
    Ziti_lib_init();

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

ziti_handle_t get_ziti_context() {
    return ziti_instance;
}
