/*
 * SSN Transport Implementation
 */

#include "ssn_transport.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>

bool ssn_address_parse(const char* address_str, ssn_address_t* addr)
{
    if (!address_str || !addr) {
        return false;
    }

    char protocol[32];
    char host[256];
    int port = 0;
    char path[256];

    const char* protocol_end = strstr(address_str, "://");
    if (!protocol_end) {
        LOG_ERROR("Invalid address format: missing '://'");
        return false;
    }

    size_t protocol_len = protocol_end - address_str;
    if (protocol_len >= sizeof(protocol)) {
        LOG_ERROR("Protocol name too long");
        return false;
    }

    strncpy(protocol, address_str, protocol_len);
    protocol[protocol_len] = '\0';

    const char* rest = protocol_end + 3;

    if (strcmp(protocol, "unix") == 0) {
        addr->type = SSN_TRANSPORT_UNIX;

        strncpy(path, rest, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';

        memset(&addr->addr.unix_addr, 0, sizeof(addr->addr.unix_addr));
        addr->addr.unix_addr.sun_family = AF_UNIX;
        strncpy(addr->addr.unix_addr.sun_path, path,
                sizeof(addr->addr.unix_addr.sun_path) - 1);

        int n = snprintf(addr->address_str, SSN_TRANSPORT_MAX_ADDRESS_LEN,
                         "unix://%s", path);
        if (n < 0 || (size_t)n >= SSN_TRANSPORT_MAX_ADDRESS_LEN) {
            LOG_ERROR("address too long: unix://%s", path);
            return false;
        }

    } else if (strcmp(protocol, "tcp") == 0) {
        addr->type = SSN_TRANSPORT_TCP;

        char* colon = strchr(rest, ':');
        if (!colon) {
            LOG_ERROR("Invalid TCP address format: missing port");
            return false;
        }

        size_t host_len = colon - rest;
        if (host_len >= sizeof(host)) {
            LOG_ERROR("Host name too long");
            return false;
        }

        strncpy(host, rest, host_len);
        host[host_len] = '\0';

        port = atoi(colon + 1);
        if (port <= 0 || port > 65535) {
            LOG_ERROR("Invalid TCP port: %d", port);
            return false;
        }

        memset(&addr->addr.inet_addr, 0, sizeof(addr->addr.inet_addr));
        addr->addr.inet_addr.sin_family = AF_INET;
        addr->addr.inet_addr.sin_port = htons((uint16_t)port);

        if (inet_pton(AF_INET, host, &addr->addr.inet_addr.sin_addr) <= 0) {
            struct hostent* he = gethostbyname(host);
            if (!he) {
                LOG_ERROR("Failed to resolve host: %s", host);
                return false;
            }
            memcpy(&addr->addr.inet_addr.sin_addr,
                   he->h_addr_list[0], he->h_length);
        }

        int n = snprintf(addr->address_str, SSN_TRANSPORT_MAX_ADDRESS_LEN,
                         "tcp://%s:%d", host, port);
        if (n < 0 || (size_t)n >= SSN_TRANSPORT_MAX_ADDRESS_LEN) {
            LOG_ERROR("address too long: tcp://%s:%d", host, port);
            return false;
        }

    } else if (strcmp(protocol, "udp") == 0) {
        addr->type = SSN_TRANSPORT_UDP;

        char* colon = strchr(rest, ':');
        if (!colon) {
            LOG_ERROR("Invalid UDP address format: missing port");
            return false;
        }

        size_t host_len = colon - rest;
        if (host_len >= sizeof(host)) {
            LOG_ERROR("Host name too long");
            return false;
        }

        strncpy(host, rest, host_len);
        host[host_len] = '\0';

        port = atoi(colon + 1);
        if (port <= 0 || port > 65535) {
            LOG_ERROR("Invalid UDP port: %d", port);
            return false;
        }

        memset(&addr->addr.inet_addr, 0, sizeof(addr->addr.inet_addr));
        addr->addr.inet_addr.sin_family = AF_INET;
        addr->addr.inet_addr.sin_port = htons((uint16_t)port);

        if (inet_pton(AF_INET, host, &addr->addr.inet_addr.sin_addr) <= 0) {
            struct hostent* he = gethostbyname(host);
            if (!he) {
                LOG_ERROR("Failed to resolve host: %s", host);
                return false;
            }
            memcpy(&addr->addr.inet_addr.sin_addr,
                   he->h_addr_list[0], he->h_length);
        }

        int n = snprintf(addr->address_str, SSN_TRANSPORT_MAX_ADDRESS_LEN,
                         "udp://%s:%d", host, port);
        if (n < 0 || (size_t)n >= SSN_TRANSPORT_MAX_ADDRESS_LEN) {
            LOG_ERROR("address too long: udp://%s:%d", host, port);
            return false;
        }

    } else if (strcmp(protocol, "tcp6") == 0) {
        addr->type = SSN_TRANSPORT_TCP6;

        char* colon = strchr(rest, ':');
        if (!colon) {
            LOG_ERROR("Invalid TCP6 address format: missing port");
            return false;
        }

        size_t host_len = colon - rest;
        if (host_len >= sizeof(host)) {
            LOG_ERROR("Host name too long");
            return false;
        }

        strncpy(host, rest, host_len);
        host[host_len] = '\0';

        port = atoi(colon + 1);
        if (port <= 0 || port > 65535) {
            LOG_ERROR("Invalid TCP6 port: %d", port);
            return false;
        }

        memset(&addr->addr.inet6_addr, 0, sizeof(addr->addr.inet6_addr));
        addr->addr.inet6_addr.sin6_family = AF_INET6;
        addr->addr.inet6_addr.sin6_port = htons((uint16_t)port);

        if (inet_pton(AF_INET6, host, &addr->addr.inet6_addr.sin6_addr) <= 0) {
            LOG_ERROR("Invalid IPv6 address: %s", host);
            return false;
        }

        int n = snprintf(addr->address_str, SSN_TRANSPORT_MAX_ADDRESS_LEN,
                         "tcp6://[%s]:%d", host, port);
        if (n < 0 || (size_t)n >= SSN_TRANSPORT_MAX_ADDRESS_LEN) {
            LOG_ERROR("address too long: tcp6://[%s]:%d", host, port);
            return false;
        }

    } else if (strcmp(protocol, "udp6") == 0) {
        addr->type = SSN_TRANSPORT_UDP6;

        char* colon = strchr(rest, ':');
        if (!colon) {
            LOG_ERROR("Invalid UDP6 address format: missing port");
            return false;
        }

        size_t host_len = colon - rest;
        if (host_len >= sizeof(host)) {
            LOG_ERROR("Host name too long");
            return false;
        }

        strncpy(host, rest, host_len);
        host[host_len] = '\0';

        port = atoi(colon + 1);
        if (port <= 0 || port > 65535) {
            LOG_ERROR("Invalid UDP6 port: %d", port);
            return false;
        }

        memset(&addr->addr.inet6_addr, 0, sizeof(addr->addr.inet6_addr));
        addr->addr.inet6_addr.sin6_family = AF_INET6;
        addr->addr.inet6_addr.sin6_port = htons((uint16_t)port);

        if (inet_pton(AF_INET6, host, &addr->addr.inet6_addr.sin6_addr) <= 0) {
            LOG_ERROR("Invalid IPv6 address: %s", host);
            return false;
        }

        int n = snprintf(addr->address_str, SSN_TRANSPORT_MAX_ADDRESS_LEN,
                         "udp6://[%s]:%d", host, port);
        if (n < 0 || (size_t)n >= SSN_TRANSPORT_MAX_ADDRESS_LEN) {
            LOG_ERROR("address too long: udp6://[%s]:%d", host, port);
            return false;
        }

    } else {
        LOG_ERROR("Unsupported protocol: %s", protocol);
        return false;
    }

    return true;
}

bool ssn_address_to_string(const ssn_address_t* addr, char* buffer, size_t size)
{
    if (!addr || !buffer || size == 0) {
        return false;
    }

    switch (addr->type) {
        case SSN_TRANSPORT_UNIX:
            snprintf(buffer, size, "unix://%s",
                     addr->addr.unix_addr.sun_path);
            break;

        case SSN_TRANSPORT_TCP: {
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr->addr.inet_addr.sin_addr,
                      ip_str, sizeof(ip_str));
            snprintf(buffer, size, "tcp://%s:%d", ip_str,
                     ntohs(addr->addr.inet_addr.sin_port));
            break;
        }

        case SSN_TRANSPORT_TCP6: {
            char ip_str[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &addr->addr.inet6_addr.sin6_addr,
                      ip_str, sizeof(ip_str));
            snprintf(buffer, size, "tcp6://[%s]:%d", ip_str,
                     ntohs(addr->addr.inet6_addr.sin6_port));
            break;
        }

        case SSN_TRANSPORT_UDP: {
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr->addr.inet_addr.sin_addr,
                      ip_str, sizeof(ip_str));
            snprintf(buffer, size, "udp://%s:%d", ip_str,
                     ntohs(addr->addr.inet_addr.sin_port));
            break;
        }

        case SSN_TRANSPORT_UDP6: {
            char ip_str[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &addr->addr.inet6_addr.sin6_addr,
                      ip_str, sizeof(ip_str));
            snprintf(buffer, size, "udp6://[%s]:%d", ip_str,
                     ntohs(addr->addr.inet6_addr.sin6_port));
            break;
        }

        default:
            LOG_ERROR("Unsupported transport type: %d", addr->type);
            return false;
    }

    return true;
}

bool ssn_address_copy(const ssn_address_t* src, ssn_address_t* dst)
{
    if (!src || !dst) {
        return false;
    }

    memcpy(dst, src, sizeof(ssn_address_t));
    return true;
}

bool ssn_address_equal(const ssn_address_t* addr1, const ssn_address_t* addr2)
{
    if (!addr1 || !addr2) {
        return false;
    }

    if (addr1->type != addr2->type) {
        return false;
    }

    switch (addr1->type) {
        case SSN_TRANSPORT_UNIX:
            return strcmp(addr1->addr.unix_addr.sun_path,
                          addr2->addr.unix_addr.sun_path) == 0;

        case SSN_TRANSPORT_TCP:
        case SSN_TRANSPORT_UDP:
            return addr1->addr.inet_addr.sin_port ==
                       addr2->addr.inet_addr.sin_port &&
                   memcmp(&addr1->addr.inet_addr.sin_addr,
                          &addr2->addr.inet_addr.sin_addr,
                          sizeof(addr1->addr.inet_addr.sin_addr)) == 0;

        case SSN_TRANSPORT_TCP6:
        case SSN_TRANSPORT_UDP6:
            return addr1->addr.inet6_addr.sin6_port ==
                       addr2->addr.inet6_addr.sin6_port &&
                   memcmp(&addr1->addr.inet6_addr.sin6_addr,
                          &addr2->addr.inet6_addr.sin6_addr,
                          sizeof(addr1->addr.inet6_addr.sin6_addr)) == 0;

        default:
            return false;
    }
}

const char* ssn_transport_type_to_string(ssn_transport_type_t type)
{
    switch (type) {
        case SSN_TRANSPORT_UNIX:
            return "unix";
        case SSN_TRANSPORT_TCP:
            return "tcp";
        case SSN_TRANSPORT_TCP6:
            return "tcp6";
        case SSN_TRANSPORT_UDP:
            return "udp";
        case SSN_TRANSPORT_UDP6:
            return "udp6";
        case SSN_TRANSPORT_TLS:
            return "tls";
        case SSN_TRANSPORT_DTLS:
            return "dtls";
        default:
            return "unknown";
    }
}

ssn_transport_type_t ssn_transport_type_from_string(const char* type_str)
{
    if (!type_str) {
        return SSN_TRANSPORT_UNIX;
    }

    if (strcmp(type_str, "unix") == 0) {
        return SSN_TRANSPORT_UNIX;
    } else if (strcmp(type_str, "tcp") == 0) {
        return SSN_TRANSPORT_TCP;
    } else if (strcmp(type_str, "tcp6") == 0) {
        return SSN_TRANSPORT_TCP6;
    } else if (strcmp(type_str, "udp") == 0) {
        return SSN_TRANSPORT_UDP;
    } else if (strcmp(type_str, "udp6") == 0) {
        return SSN_TRANSPORT_UDP6;
    } else if (strcmp(type_str, "tls") == 0) {
        return SSN_TRANSPORT_TLS;
    } else if (strcmp(type_str, "dtls") == 0) {
        return SSN_TRANSPORT_DTLS;
    }

    return SSN_TRANSPORT_UNIX;
}

int ssn_transport_get_fd(const ssn_transport_t* transport)
{
    if (!transport || !transport->valid) {
        return -1;
    }
    
    // 尝试通过get_option获取文件描述符
    int fd = -1;
    if (transport->ops.get_option) {
        // 注意：这里的SO_RCVBUF选项可能不是获取文件描述符的正确方式
        // 但为了保持兼容性，暂时使用这种方式
        transport->ops.get_option(transport, 0, &fd);
    }
    
    return fd;
}

