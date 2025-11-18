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
#include <cstring>
#include <map>
#include <dlfcn.h>
#include <arpa/inet.h>
#include <netinet/in.h>

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

// ============================================================================
// c-ares compatible hooks with C linkage for LD_PRELOAD support
// These provide c-ares functionality without linking to c-ares library

// Define c-ares error codes (from ares.h) to avoid linking to c-ares
#ifndef ARES_SUCCESS
#define ARES_SUCCESS 0
#endif
#ifndef ARES_ENOTFOUND
#define ARES_ENOTFOUND 11
#endif
#ifndef ARES_ENOMEM
#define ARES_ENOMEM 12
#endif

extern "C" {
    // Original c-ares function pointers (may be null if statically linked)
    static void (*original_ares_query)(ares_channel_t *channel, const char *name, int dnsclass, int type, ares_callback callback, void *arg) = nullptr;
    static void (*original_ares_gethostbyname)(ares_channel_t *channel, const char *name, int family, ares_host_callback callback, void *arg) = nullptr;
    static bool function_ptrs_initialized = false;
    
    // Thread-local recursion guards to prevent infinite loops
    static thread_local bool in_ares_query = false;
    static thread_local bool in_ares_gethostbyname = false;
    
    // Hostname-to-fake-IP cache for c-ares compatibility
    // Problem: libziti's find_service() requires BOTH hostname AND port to match intercept configs
    // c-ares only provides hostname, so we can't match services with specific port ranges
    // Solution: Return a fake IP that connect() can map back to the hostname
    // Then connect() has BOTH hostname (from cache) and port (from sockaddr) for Ziti_connect_addr()
    static std::map<std::string, in_addr_t> hostname_to_ip_cache;
    static std::map<in_addr_t, std::string> ip_to_hostname_cache;
    static in_addr_t next_fake_ip = 0x64400000; // Start at 100.64.0.0 (CGNAT range, same as libziti)
    static uv_mutex_t cache_mutex;
    static uv_once_t cache_init_once = UV_ONCE_INIT;
    
    static void init_cache() {
        uv_mutex_init(&cache_mutex);
    }
    
    // Get or create a fake IP for a hostname (for c-ares compatibility)
    static in_addr_t get_fake_ip_for_hostname(const char *hostname) {
        uv_once(&cache_init_once, init_cache);
        uv_mutex_lock(&cache_mutex);
        
        std::string host_str(hostname);
        auto it = hostname_to_ip_cache.find(host_str);
        if (it != hostname_to_ip_cache.end()) {
            uv_mutex_unlock(&cache_mutex);
            return it->second;
        }
        
        // Allocate new fake IP
        in_addr_t fake_ip = next_fake_ip;
        next_fake_ip = htonl(ntohl(next_fake_ip) + 1);
        hostname_to_ip_cache[host_str] = fake_ip;
        ip_to_hostname_cache[fake_ip] = host_str;
        
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &fake_ip, ip_str, sizeof(ip_str));
        ZITI_LOG(DEBUG, "Allocated fake IP %s for hostname %s (c-ares has no port context)", ip_str, hostname);
        
        uv_mutex_unlock(&cache_mutex);
        return fake_ip;
    }
    
    // Look up hostname by fake IP (called from connect())
    const char* lookup_hostname_by_fake_ip(in_addr_t ip) {
        uv_once(&cache_init_once, init_cache);
        uv_mutex_lock(&cache_mutex);
        
        auto it = ip_to_hostname_cache.find(ip);
        const char *result = (it != ip_to_hostname_cache.end()) ? it->second.c_str() : nullptr;
        
        uv_mutex_unlock(&cache_mutex);
        return result;
    }
    
    // Helper function to initialize function pointers safely
    static void initialize_function_pointers() {
        if (function_ptrs_initialized) return;
        
        original_ares_query = (void(*)(ares_channel_t*, const char*, int, int, ares_callback, void*))
            dlsym(RTLD_NEXT, "ares_query");
        original_ares_gethostbyname = (void(*)(ares_channel_t*, const char*, int, ares_host_callback, void*))
            dlsym(RTLD_NEXT, "ares_gethostbyname");
        
        function_ptrs_initialized = true;
        
        ZITI_LOG(DEBUG, "C-ares function pointers initialized: ares_query=%p, ares_gethostbyname=%p", 
                 original_ares_query, original_ares_gethostbyname);
        
        if (!original_ares_query && !original_ares_gethostbyname) {
            ZITI_LOG(INFO, "C-ares functions not found via RTLD_NEXT (likely statically linked). "
                     "Will perform Ziti resolution directly for intercepted queries.");
        }
    }
    
    // Helper to convert addrinfo to hostent for c-ares callback
    static struct hostent* addrinfo_to_hostent(const struct addrinfo *ai, int family) {
        if (!ai) return nullptr;
        
        // Allocate hostent structure (caller must free)
        struct hostent *host = (struct hostent*)calloc(1, sizeof(struct hostent));
        if (!host) return nullptr;
        
        // Find matching address family
        const struct addrinfo *current = ai;
        while (current && current->ai_family != family) {
            current = current->ai_next;
        }
        
        if (!current) {
            free(host);
            return nullptr;
        }
        
        // Allocate and copy hostname
        if (current->ai_canonname) {
            host->h_name = strdup(current->ai_canonname);
        }
        
        host->h_addrtype = family;
        host->h_length = (family == AF_INET) ? 4 : 16;
        
        // Count addresses
        int addr_count = 0;
        for (const struct addrinfo *p = current; p && p->ai_family == family; p = p->ai_next) {
            addr_count++;
        }
        
        // Allocate address list
        host->h_addr_list = (char**)calloc(addr_count + 1, sizeof(char*));
        if (!host->h_addr_list) {
            free(host->h_name);
            free(host);
            return nullptr;
        }
        
        // Copy addresses
        int i = 0;
        for (const struct addrinfo *p = current; p && p->ai_family == family && i < addr_count; p = p->ai_next) {
            host->h_addr_list[i] = (char*)malloc(host->h_length);
            if (host->h_addr_list[i]) {
                if (family == AF_INET) {
                    memcpy(host->h_addr_list[i], &((struct sockaddr_in*)p->ai_addr)->sin_addr, host->h_length);
                } else {
                    memcpy(host->h_addr_list[i], &((struct sockaddr_in6*)p->ai_addr)->sin6_addr, host->h_length);
                }
                i++;
            }
        }
        
        return host;
    }
    
    // Helper to free hostent created by addrinfo_to_hostent
    static void free_hostent(struct hostent *host) {
        if (!host) return;
        free(host->h_name);
        if (host->h_addr_list) {
            for (int i = 0; host->h_addr_list[i]; i++) {
                free(host->h_addr_list[i]);
            }
            free(host->h_addr_list);
        }
        free(host);
    }
    
    // c-ares compatible ares_query hook (C linkage for LD_PRELOAD)
    void ares_query(ares_channel_t *channel, const char *name, int dnsclass, int type, ares_callback callback, void *arg) {
        // Prevent recursive calls
        if (in_ares_query) {
            ZITI_LOG(TRACE, "ares_query: recursive call detected, skipping");
            return;
        }
        in_ares_query = true;
        
        initialize_function_pointers();
        
        ZITI_LOG(DEBUG, "c-ares compatible ares_query for %s (type=%d, class=%d)", name, type, dnsclass);
        
        // If original function exists, use it (c-ares is dynamically linked)
        if (original_ares_query) {
            ZITI_LOG(TRACE, "Delegating to original ares_query");
            original_ares_query(channel, name, dnsclass, type, callback, arg);
            in_ares_query = false;
            return;
        }
        
        // c-ares is statically linked - we must handle the query ourselves
        // Note: ares_query is lower-level than ares_gethostbyname and requires
        // constructing DNS wire format responses. For now, signal ARES_ENOTFOUND
        // and let ares_gethostbyname handle the actual resolution.
        ZITI_LOG(DEBUG, "ares_query: no original function, returning ARES_ENOTFOUND to trigger fallback");
        if (callback) {
            callback(arg, ARES_ENOTFOUND, 0, nullptr, 0);
        }
        
        in_ares_query = false;
    }
    
    // c-ares compatible ares_gethostbyname hook (C linkage for LD_PRELOAD)
    void ares_gethostbyname(ares_channel_t *channel, const char *name, int family, ares_host_callback callback, void *arg) {
        // Prevent recursive calls
        if (in_ares_gethostbyname) {
            ZITI_LOG(TRACE, "ares_gethostbyname: recursive call detected, skipping");
            return;
        }
        in_ares_gethostbyname = true;
        
        initialize_function_pointers();
        
        ZITI_LOG(DEBUG, "c-ares compatible ares_gethostbyname for %s (family=%d)", name, family);
        
        // If original function exists, use it (c-ares is dynamically linked)
        if (original_ares_gethostbyname) {
            ZITI_LOG(TRACE, "Delegating to original ares_gethostbyname");
            original_ares_gethostbyname(channel, name, family, callback, arg);
            in_ares_gethostbyname = false;
            return;
        }
        
        // c-ares is statically linked - return fake IP for later resolution
        // Problem: libziti's find_service() needs BOTH hostname AND port to match intercept configs
        // c-ares only gives us hostname, so we can't call Ziti_resolve() successfully
        // Solution: Return a fake IP that connect() can map back to hostname+port
        ZITI_LOG(DEBUG, "c-ares query for %s (family=%d) - returning fake IP for connect() lookup", name, family);
        
        in_addr_t fake_ip = get_fake_ip_for_hostname(name);
        
        // Create a hostent with the fake IP
        struct hostent *host = (struct hostent*)calloc(1, sizeof(struct hostent));
        if (host) {
            host->h_name = strdup(name);
            host->h_addrtype = family;
            host->h_length = (family == AF_INET) ? 4 : 16;
            host->h_addr_list = (char**)calloc(2, sizeof(char*));
            
            if (host->h_addr_list) {
                host->h_addr_list[0] = (char*)malloc(host->h_length);
                if (host->h_addr_list[0]) {
                    if (family == AF_INET) {
                        memcpy(host->h_addr_list[0], &fake_ip, 4);
                    } else {
                        // For IPv6, create a v4-mapped address
                        struct in6_addr v6addr = {0};
                        v6addr.s6_addr32[2] = htonl(0xffff);
                        v6addr.s6_addr32[3] = fake_ip;
                        memcpy(host->h_addr_list[0], &v6addr, 16);
                    }
                    
                    if (callback) {
                        callback(arg, ARES_SUCCESS, 0, host);
                    }
                }
            }
            free_hostent(host);
        } else {
            if (callback) {
                callback(arg, ARES_ENOMEM, 0, nullptr);
            }
        }
        
        in_ares_gethostbyname = false;
    }
}
