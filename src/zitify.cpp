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
#include <netdb.h>
#include <ares.h>
#include <dlfcn.h>
#include <arpa/nameser.h>
#include <arpa/inet.h>
#include <cstring>

#include <ziti/zitilib.h>
#include <ziti/ziti_log.h>
#include <ziti/model_collections.h>

#include "std_funcs.h"
#include "zitify.h"

// ============================================================================
// Forward Declarations for Dynamic Fake IP to Service Mapping
// ============================================================================
// These functions map fake IP addresses to their corresponding Ziti service names,
// allowing the connect() function to properly route connections back to Ziti.

#include <unordered_map>
static uint32_t get_fake_ip_for_service(const char* service_name);
static const char* get_service_for_fake_ip(uint32_t fake_ip);


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
        ZITI_LOG(DEBUG,"fallback getnameinfo");
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
    
    // Check if this is a fake IP from our C-ares hooks
    const char* mapped_service = get_service_for_fake_ip(in_addr);
    if (mapped_service != nullptr) {
        ZITI_LOG(DEBUG, "Detected fake Ziti IP: %s, attempting Ziti connection to service: %s", addrbuf, mapped_service);
        hostname = mapped_service;
    } else if (in_addr == 0 || (hostname = Ziti_lookup(in_addr)) == nullptr) {
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

// ============================================================================
// Dynamic Fake IP to Service Mapping
// ============================================================================
// This system maps fake IP addresses to their corresponding Ziti service names,
// allowing the connect() function to properly route connections back to Ziti.

#include <unordered_map>
static std::unordered_map<uint32_t, std::string> fake_ip_to_service_map;
static uint32_t next_fake_ip = htonl(0x7ffe0101); // Start with 127.254.1.1

// Helper function to get or allocate a fake IP for a Ziti service
static uint32_t get_fake_ip_for_service(const char* service_name) {
    if (!service_name) return htonl(0x7ffe0101); // Default fallback
    
    std::string service(service_name);
    
    // Check if we already have a mapping for this service
    for (const auto& pair : fake_ip_to_service_map) {
        if (pair.second == service) {
            return pair.first;
        }
    }
    
    // Allocate a new fake IP
    uint32_t fake_ip = next_fake_ip++;
    fake_ip_to_service_map[fake_ip] = service;
    
    char ip_str[INET_ADDRSTRLEN];
    uv_inet_ntop(AF_INET, &fake_ip, ip_str, sizeof(ip_str));
    ZITI_LOG(DEBUG, "Allocated fake IP %s for service %s", ip_str, service_name);
    
    return fake_ip;
}

// Helper function to check if an IP is in our fake IP range
static bool is_fake_ip_range(uint32_t ip) {
    // Convert to host byte order for comparison
    uint32_t ip_host = ntohl(ip);
    uint32_t fake_ip_start = 0x7ffe0101; // 127.254.1.1 in host byte order
    uint32_t fake_ip_end = 0x7fefffff;   // 127.254.255.255 in host byte order
    
    return (ip_host >= fake_ip_start && ip_host <= fake_ip_end);
}

// Helper function to find the service name for a fake IP
static const char* get_service_for_fake_ip(uint32_t fake_ip) {
    auto it = fake_ip_to_service_map.find(fake_ip);
    if (it != fake_ip_to_service_map.end()) {
        return it->second.c_str();
    }
    
    // If not found in mapping but it's in our fake IP range,
    // it might be a direct IP connection. Try to find a default service.
    if (is_fake_ip_range(fake_ip)) {
        struct in_addr addr;
        addr.s_addr = fake_ip;
        ZITI_LOG(DEBUG, "IP %s is in fake IP range but not mapped, checking for default Ziti service", 
                 inet_ntoa(addr));
        
        // For now, return the first mapped service as a fallback
        // In a production environment, this could be more sophisticated
        if (!fake_ip_to_service_map.empty()) {
            const char* default_service = fake_ip_to_service_map.begin()->second.c_str();
            ZITI_LOG(DEBUG, "Using default service %s for unmapped fake IP", default_service);
            return default_service;
        }
    }
    
    return nullptr;
}

// ============================================================================
// C-ares DNS Resolution Hooks
// ============================================================================
// These hooks intercept gRPC's C-ares DNS resolution calls to provide
// fake IP addresses for Ziti domains, which will then be intercepted
// by the existing connect() hook.

extern "C" {
    // Original C-ares function pointers
    static void (*original_ares_query)(ares_channel_t *channel, const char *name, int dnsclass, int type, ares_callback callback, void *arg) = nullptr;
    static void (*original_ares_gethostbyname)(ares_channel_t *channel, const char *name, int family, ares_host_callback callback, void *arg) = nullptr;
    static bool function_ptrs_initialized = false;
    
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
    }
    
    // Helper function to check if hostname is a Ziti intercept domain using Ziti_resolve
    static bool is_ziti_intercept_domain(const char* name, char** service_name) {
        if (!name) return false;
        
        // Use Ziti_resolve to check if this is a Ziti intercept domain
        struct addrinfo* result = nullptr;
        struct addrinfo hints = {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        
        // Convert port to string for Ziti_resolve
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", 10001); // Default port, could be made configurable
        
        int rc = Ziti_resolve(name, port_str, &hints, &result);
        if (rc == 0 && result != nullptr) {
            ZITI_LOG(DEBUG, "Ziti_resolve found intercept domain: %s", name);
            
            // Extract the service name from the result for connect() mapping
            if (service_name != nullptr && result->ai_canonname != nullptr) {
                *service_name = strdup(result->ai_canonname);
                ZITI_LOG(DEBUG, "Mapped domain %s to service %s", name, *service_name);
            }
            
            // Clean up
            freeaddrinfo(result);
            return true;
        }
        
        return false;
    }
    
    // Helper function to create a fake DNS response for Ziti domains
    static void create_fake_dns_response(const char* name, ares_callback callback, void *arg) {
        ZITI_LOG(DEBUG, "Creating fake DNS response for Ziti domain: %s", name);
        
        // Get the service name for this domain
        char* service_name = nullptr;
        if (!is_ziti_intercept_domain(name, &service_name) || !service_name) {
            // Fallback to default if service lookup fails
            service_name = strdup(name);
        }
        
        // Get a dynamic fake IP for this service
        uint32_t fake_ip = get_fake_ip_for_service(service_name);
        free(service_name);
        
        // For simplicity, we'll construct a basic DNS response
        // In a production implementation, this would need to be more robust
        unsigned char fake_response[512];
        int response_len = 0;
        
        // Minimal DNS header (simplified)
        memset(fake_response, 0, sizeof(fake_response));
        // Transaction ID
        fake_response[0] = 0x12;
        fake_response[1] = 0x34;
        // Flags: Response, No Error
        fake_response[2] = 0x81;
        fake_response[3] = 0x80;
        // Questions: 1, Answers: 1
        fake_response[4] = 0x00;
        fake_response[5] = 0x01;
        fake_response[6] = 0x00;
        fake_response[7] = 0x01;
        
        // Query section (simplified - would need proper encoding)
        response_len = 12;
        
        // Answer section with dynamic fake IP
        fake_response[response_len++] = 0xc0; // Name pointer
        fake_response[response_len++] = 0x0c; // to query name
        fake_response[response_len++] = 0x00; // Type A
        fake_response[response_len++] = 0x01;
        fake_response[response_len++] = 0x00; // Class IN
        fake_response[response_len++] = 0x01;
        fake_response[response_len++] = 0x00; // TTL
        fake_response[response_len++] = 0x00;
        fake_response[response_len++] = 0x00;
        fake_response[response_len++] = 0x3c;
        fake_response[response_len++] = 0x00; // Data length
        fake_response[response_len++] = 0x04;
        // Add the dynamic fake IP
        fake_response[response_len++] = (fake_ip >> 0) & 0xFF;
        fake_response[response_len++] = (fake_ip >> 8) & 0xFF;
        fake_response[response_len++] = (fake_ip >> 16) & 0xFF;
        fake_response[response_len++] = (fake_ip >> 24) & 0xFF;
        
        callback(arg, ARES_SUCCESS, 0, fake_response, response_len);
    }
    
    // Helper function to create a fake hostent for Ziti domains
    static void create_fake_hostent(const char* name, ares_host_callback callback, void *arg) {
        ZITI_LOG(DEBUG, "Creating fake hostent for Ziti domain: %s", name);
        
        // Get the service name for this domain
        char* service_name = nullptr;
        if (!is_ziti_intercept_domain(name, &service_name) || !service_name) {
            // Fallback to default if service lookup fails
            service_name = strdup(name);
        }
        
        // Get a dynamic fake IP for this service
        uint32_t fake_ip = get_fake_ip_for_service(service_name);
        free(service_name);
        
        // Create a fake hostent structure
        static char fake_name[256];
        static char* fake_aliases[2] = { nullptr, nullptr };
        static struct in_addr fake_addr;
        static char* fake_addr_list[2] = { (char*)&fake_addr, nullptr };
        static struct hostent fake_host;
        
        strncpy(fake_name, name, sizeof(fake_name) - 1);
        fake_name[sizeof(fake_name) - 1] = '\0';
        
        fake_addr.s_addr = fake_ip; // Use dynamic fake IP
        
        fake_host.h_name = fake_name;
        fake_host.h_aliases = fake_aliases;
        fake_host.h_addrtype = AF_INET;
        fake_host.h_length = sizeof(struct in_addr);
        fake_host.h_addr_list = fake_addr_list;
        
        callback(arg, ARES_SUCCESS, 0, &fake_host);
    }
    
    // Hooked ares_query function
    void ares_query(ares_channel_t *channel, const char *name, int dnsclass, int type, ares_callback callback, void *arg) {
        initialize_function_pointers();
        
        ZITI_LOG(DEBUG, "Hooked ares_query for %s (type=%d)", name, type);
        
        // Check if this is a Ziti intercept domain we should handle
        if (is_ziti_intercept_domain(name, nullptr)) {
            ZITI_LOG(DEBUG, "Intercepting Ziti domain query: %s", name);
            
            if (type == ns_t_a || type == ns_t_aaaa) {
                // Provide fake DNS response for A/AAAA queries
                create_fake_dns_response(name, callback, arg);
                return;
            }
        }
        
        if (original_ares_query) {
            original_ares_query(channel, name, dnsclass, type, callback, arg);
        }
    }
    
    // Hooked ares_gethostbyname function
    void ares_gethostbyname(ares_channel_t *channel, const char *name, int family, ares_host_callback callback, void *arg) {
        initialize_function_pointers();
        
        ZITI_LOG(DEBUG, "Hooked ares_gethostbyname for %s (family=%d, AF_INET=%d, AF_INET6=%d, AF_UNSPEC=%d)", 
                 name, family, AF_INET, AF_INET6, AF_UNSPEC);
        
        // Check if this is a Ziti intercept domain we should handle
        bool is_ziti = is_ziti_intercept_domain(name, nullptr);
        bool valid_family = (family == AF_INET || family == AF_INET6 || family == AF_UNSPEC);
        ZITI_LOG(DEBUG, "Domain check: is_ziti=%s, valid_family=%s", 
                 is_ziti ? "true" : "false", valid_family ? "true" : "false");
        
        if (is_ziti && valid_family) {
            ZITI_LOG(DEBUG, "Intercepting Ziti host lookup: %s", name);
            
            // Provide fake hostent
            create_fake_hostent(name, callback, arg);
            return;
        }
        
        if (original_ares_gethostbyname) {
            original_ares_gethostbyname(channel, name, family, callback, arg);
        }
    }
}
