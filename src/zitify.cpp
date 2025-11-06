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
    
    // Use Ziti_lookup to find if this IP maps to a Ziti service (SDK's CG-NAT range)
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

// ============================================================================
// These hooks intercept C-ares DNS resolution calls to provide
// intercepted IP addresses for Ziti domains, which will then be recognized
// by the connect() hook.

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
    
    // Helper function to create a fake DNS response for Ziti domains
    static void create_ziti_dns_response(const char* name, const char* port, ares_channel_t *channel, int dnsclass, int type, ares_callback callback, void *arg) {
        ZITI_LOG(DEBUG, "Creating Ziti DNS response for domain: %s:%s", name, port);
        
        // Use Ziti_resolve to get the SDK's fake IP
        struct addrinfo *result = nullptr;
        int rc = Ziti_resolve(name, port, nullptr, &result);
        
        if (rc != 0 || result == nullptr) {
            ZITI_LOG(DEBUG, "Ziti_resolve failed for %s:%s, falling back", name, port);
            // Fall back to original C-ares query
            if (original_ares_query) {
                original_ares_query(channel, name, dnsclass, type, callback, arg);
            }
            return;
        }
        
        // Extract the fake IP from Ziti_resolve's response
        if (result->ai_addr && result->ai_family == AF_INET) {
            auto addr4 = (sockaddr_in *) result->ai_addr;
            uint32_t ziti_fake_ip = addr4->sin_addr.s_addr;
            
            char ip_str[INET_ADDRSTRLEN];
            uv_inet_ntop(AF_INET, &ziti_fake_ip, ip_str, sizeof(ip_str));
            ZITI_LOG(DEBUG, "Ziti_resolve assigned %s:%s => %s", name, port, ip_str);
            
            // Construct DNS response with the Ziti SDK's fake IP
            unsigned char dns_response[512];
            int response_len = 0;
            
            // Minimal DNS header
            memset(dns_response, 0, sizeof(dns_response));
            dns_response[0] = 0x12;  // Transaction ID
            dns_response[1] = 0x34;
            dns_response[2] = 0x81;  // Response, Authoritative, No Error
            dns_response[3] = 0x80;
            dns_response[4] = 0x00;  // Questions: 1
            dns_response[5] = 0x01;
            dns_response[6] = 0x00;  // Answers: 1  
            dns_response[7] = 0x01;
            response_len = 8;
            
            // Copy original question (simplified - in production would preserve original)
            // For now, just add the answer section
            dns_response[response_len++] = 0xc0;  // Pointer to question name
            dns_response[response_len++] = 0x0c;
            dns_response[response_len++] = 0x00;  // Type: A record
            dns_response[response_len++] = 0x01;
            dns_response[response_len++] = 0x00;  // Class: IN
            dns_response[response_len++] = 0x01;
            dns_response[response_len++] = 0x00;  // TTL (4 bytes)
            dns_response[response_len++] = 0x00;
            dns_response[response_len++] = 0x0e;
            dns_response[response_len++] = 0x10;
            dns_response[response_len++] = 0x00;  // Data length: 4
            dns_response[response_len++] = 0x04;
            
            // Copy the fake IP address
            memcpy(&dns_response[response_len], &ziti_fake_ip, 4);
            response_len += 4;
            
            // Call the callback with our DNS response
            callback(arg, ARES_SUCCESS, 0, dns_response, response_len);
            
        } else {
            ZITI_LOG(DEBUG, "Ziti_resolve returned non-IPv4 address for %s:%s", name, port);
            // Fall back to original C-ares
            if (original_ares_query) {
                original_ares_query(channel, name, dnsclass, type, callback, arg);
            }
        }
        
        // Clean up
        if (result) {
            freeaddrinfo(result);
        }
    }
    
    // Helper function to create a fake hostent for Ziti domains
    static void create_ziti_hostent(const char* name, struct addrinfo *result, ares_host_callback callback, void *arg) {
        ZITI_LOG(DEBUG, "Creating Ziti hostent for domain: %s", name);
        
        // Use the provided result instead of calling Ziti_resolve again
        if (!result) {
            ZITI_LOG(DEBUG, "No result provided for %s, falling back", name);
            // Fall back to original C-ares query
            if (original_ares_gethostbyname) {
                original_ares_gethostbyname(nullptr, name, AF_INET, callback, arg);
            }
            return;
        }
        
        // Extract the fake IP from the provided result
        if (result->ai_addr && result->ai_family == AF_INET) {
            auto addr4 = (sockaddr_in *) result->ai_addr;
            uint32_t ziti_fake_ip = addr4->sin_addr.s_addr;
            
            char ip_str[INET_ADDRSTRLEN];
            uv_inet_ntop(AF_INET, &ziti_fake_ip, ip_str, sizeof(ip_str));
            ZITI_LOG(DEBUG, "Using cached Ziti_resolve result for %s => %s", name, ip_str);
            
            // Use static allocation to avoid memory management issues
            static char h_name_buf[256];
            static char* h_aliases_buf[2] = { nullptr };
            static struct in_addr h_addr_list_buf[2];
            static char* h_addr_ptrs[2];
            
            // Set up the hostent structure
            strncpy(h_name_buf, name, sizeof(h_name_buf) - 1);
            h_name_buf[sizeof(h_name_buf) - 1] = '\0';
            
            // Set up the fake IP address
            h_addr_list_buf[0].s_addr = ziti_fake_ip;
            h_addr_list_buf[1].s_addr = 0; // Null terminator
            
            // Set up address pointers
            h_addr_ptrs[0] = (char*)&h_addr_list_buf[0];
            h_addr_ptrs[1] = nullptr;
            
            // Create the hostent structure
            struct hostent hent;
            hent.h_name = h_name_buf;
            hent.h_aliases = h_aliases_buf;
            hent.h_addrtype = AF_INET;
            hent.h_length = sizeof(struct in_addr);
            hent.h_addr_list = h_addr_ptrs;
            
            // Call the callback with our fake hostent
            callback(arg, ARES_SUCCESS, 0, &hent);
            
        } else {
            ZITI_LOG(DEBUG, "No IPv4 address in result for %s, falling back", name);
            // Fall back to original C-ares query
            if (original_ares_gethostbyname) {
                original_ares_gethostbyname(nullptr, name, AF_INET, callback, arg);
            }
        }
        
        // Clean up
        if (result) {
            freeaddrinfo(result);
        }
    }
    
    // Hooked ares_query function
    void ares_query(ares_channel_t *channel, const char *name, int dnsclass, int type, ares_callback callback, void *arg) {
        initialize_function_pointers();
        
        ZITI_LOG(DEBUG, "Hooked ares_query for %s (type=%d)", name, type);
        
        // Always attempt Ziti resolution - let Ziti_resolve determine if it's a Ziti domain
        ZITI_LOG(DEBUG, "Intercepting domain query: %s", name);
        
        if (type == ns_t_a || type == ns_t_aaaa) {
            // Use Ziti_resolve to get the SDK's fake IP and create DNS response
            create_ziti_dns_response(name, "10001", channel, dnsclass, type, callback, arg);
            return;
        }
        
        // Fall back to original C-ares query for non-Ziti domains or other record types
        if (original_ares_query) {
            original_ares_query(channel, name, dnsclass, type, callback, arg);
        }
    }
    
    // Hooked ares_gethostbyname function
    void ares_gethostbyname(ares_channel_t *channel, const char *name, int family, ares_host_callback callback, void *arg) {
        initialize_function_pointers();
        
        ZITI_LOG(DEBUG, "Hooked ares_gethostbyname for %s (family=%d, AF_INET=%d, AF_INET6=%d, AF_UNSPEC=%d)", 
                 name, family, AF_INET, AF_INET6, AF_UNSPEC);
        
        // Always attempt Ziti resolution - let Ziti_resolve determine if it's a Ziti domain
        bool valid_family = (family == AF_INET || family == AF_INET6 || family == AF_UNSPEC);
        ZITI_LOG(DEBUG, "Domain check: valid_family=%s", valid_family ? "true" : "false");
        
        if (valid_family) {
            ZITI_LOG(DEBUG, "Intercepting host lookup: %s", name);
            
            // Use Ziti_resolve to get the SDK's fake IP and create hostent response
            struct addrinfo hints = {};
            hints.ai_family = family;
            hints.ai_socktype = SOCK_STREAM;
            
            struct addrinfo *result = nullptr;
            int rc = Ziti_resolve(name, "10001", &hints, &result);
            
            if (rc == 0 && result != nullptr) {
                // Create a simple hostent structure from the addrinfo result
                static char h_name_buf[256];
                static char* h_aliases_buf[1] = { nullptr };
                static struct in_addr h_addr;
                static char* h_addr_ptrs[2];
                
                strncpy(h_name_buf, name, sizeof(h_name_buf) - 1);
                h_name_buf[sizeof(h_name_buf) - 1] = '\0';
                
                if (result->ai_addr && result->ai_family == AF_INET) {
                    auto addr4 = (sockaddr_in *) result->ai_addr;
                    h_addr = addr4->sin_addr;
                    h_addr_ptrs[0] = (char*)&h_addr;
                    h_addr_ptrs[1] = nullptr;
                    
                    struct hostent hent;
                    hent.h_name = h_name_buf;
                    hent.h_aliases = h_aliases_buf;
                    hent.h_addrtype = AF_INET;
                    hent.h_length = sizeof(struct in_addr);
                    hent.h_addr_list = h_addr_ptrs;
                    
                    char ip_str[INET_ADDRSTRLEN];
                    uv_inet_ntop(AF_INET, &h_addr, ip_str, sizeof(ip_str));
                    ZITI_LOG(DEBUG, "ares_gethostbyname returning %s => %s", name, ip_str);
                    
                    callback(arg, ARES_SUCCESS, 0, &hent);
                    freeaddrinfo(result);
                    return;
                }
            }
            
            ZITI_LOG(DEBUG, "Ziti_resolve failed for %s, falling back", name);
            freeaddrinfo(result);
        }
        
        // Fall back to original C-ares function for non-Ziti domains or invalid families
        if (original_ares_gethostbyname) {
            original_ares_gethostbyname(channel, name, family, callback, arg);
        }
    }
}
