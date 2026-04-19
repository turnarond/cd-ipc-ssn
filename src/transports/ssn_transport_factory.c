/*
 * Transport Factory Implementation
 */

#include "ssn_transport.h"
#include <stdlib.h>
#include <string.h>

typedef ssn_transport_t* (*transport_creator_t)(
    const ssn_transport_config_t* config);

typedef struct transport_factory_impl {
    ssn_hash_table_t* creators;
    ssn_mutex_t* lock;
} transport_factory_impl_t;

static transport_factory_impl_t* g_factory = NULL;

extern ssn_transport_t* unix_transport_create(
    const ssn_transport_config_t* config);
extern ssn_transport_t* tcp_transport_create(
    const ssn_transport_config_t* config, bool ipv6_enabled);
extern ssn_transport_t* udp_transport_create(
    const ssn_transport_config_t* config, bool ipv6_enabled);

static ssn_transport_t* factory_create_unix(
    const ssn_transport_config_t* config)
{
    return unix_transport_create(config);
}

static ssn_transport_t* factory_create_tcp(
    const ssn_transport_config_t* config)
{
    return tcp_transport_create(config, false);
}

static ssn_transport_t* factory_create_tcp6(
    const ssn_transport_config_t* config)
{
    return tcp_transport_create(config, true);
}

static ssn_transport_t* factory_create_udp(
    const ssn_transport_config_t* config)
{
    return udp_transport_create(config, false);
}

static ssn_transport_t* factory_create_udp6(
    const ssn_transport_config_t* config)
{
    return udp_transport_create(config, true);
}

bool ssn_transport_factory_init(void)
{
    if (g_factory) {
        return true;
    }

    g_factory = (transport_factory_impl_t*)calloc(
        1, sizeof(transport_factory_impl_t));
    if (!g_factory) {
        LOG_ERROR("Failed to allocate memory for transport factory");
        return false;
    }

    g_factory->lock = ssn_mutex_create();
    if (!g_factory->lock) {
        LOG_ERROR("Failed to create mutex for transport factory");
        free(g_factory);
        g_factory = NULL;
        return false;
    }

    g_factory->creators = ssn_hash_table_create(16);
    if (!g_factory->creators) {
        LOG_ERROR("Failed to create hash table for transport factory");
        ssn_mutex_destroy(g_factory->lock);
        free(g_factory);
        g_factory = NULL;
        return false;
    }

    ssn_hash_table_set(g_factory->creators,
                       (void*)((uintptr_t)SSN_TRANSPORT_UNIX + 1),
                       (void*)factory_create_unix);
    ssn_hash_table_set(g_factory->creators,
                       (void*)((uintptr_t)SSN_TRANSPORT_TCP + 1),
                       (void*)factory_create_tcp);
    ssn_hash_table_set(g_factory->creators,
                       (void*)((uintptr_t)SSN_TRANSPORT_TCP6 + 1),
                       (void*)factory_create_tcp6);
    ssn_hash_table_set(g_factory->creators,
                       (void*)((uintptr_t)SSN_TRANSPORT_UDP + 1),
                       (void*)factory_create_udp);
    ssn_hash_table_set(g_factory->creators,
                       (void*)((uintptr_t)SSN_TRANSPORT_UDP6 + 1),
                       (void*)factory_create_udp6);

    LOG_INFO("Transport factory initialized successfully");
    return true;
}

void ssn_transport_factory_cleanup(void)
{
    if (!g_factory) {
        return;
    }

    if (g_factory->creators) {
        ssn_hash_table_destroy(g_factory->creators);
        g_factory->creators = NULL;
    }

    if (g_factory->lock) {
        ssn_mutex_destroy(g_factory->lock);
        g_factory->lock = NULL;
    }

    free(g_factory);
    g_factory = NULL;

    LOG_INFO("Transport factory cleaned up");
}

ssn_transport_t* ssn_transport_factory_create(
    ssn_transport_type_t type,
    const ssn_transport_config_t* config)
{
    if (!g_factory) {
        if (!ssn_transport_factory_init()) {
            LOG_ERROR("Failed to initialize transport factory");
            return NULL;
        }
    }

    ssn_mutex_lock(g_factory->lock);

    transport_creator_t creator = (transport_creator_t)ssn_hash_table_get(
        g_factory->creators, (void*)((uintptr_t)type + 1));

    if (!creator) {
        LOG_ERROR("Unsupported transport type: %d", type);
        ssn_mutex_unlock(g_factory->lock);
        return NULL;
    }

    ssn_transport_t* transport = creator(config);

    ssn_mutex_unlock(g_factory->lock);

    if (transport) {
        LOG_DEBUG("Created transport instance: type=%d", type);
    }

    return transport;
}

bool ssn_transport_factory_register(
    ssn_transport_type_t type,
    ssn_transport_t* (*creator)(const ssn_transport_config_t* config))
{
    if (!g_factory) {
        if (!ssn_transport_factory_init()) {
            return false;
        }
    }

    ssn_mutex_lock(g_factory->lock);

    void* existing = ssn_hash_table_get(g_factory->creators,
                                        (void*)((uintptr_t)type + 1));
    if (existing) {
        LOG_WARN("Transport type %d already registered, replacing", type);
    }

    bool result = ssn_hash_table_set(g_factory->creators,
                                      (void*)((uintptr_t)type + 1),
                                      (void*)creator);

    ssn_mutex_unlock(g_factory->lock);

    if (result) {
        LOG_DEBUG("Registered transport creator for type %d", type);
    }

    return result;
}

bool ssn_transport_factory_is_type_supported(ssn_transport_type_t type)
{
    if (!g_factory) {
        return false;
    }

    ssn_mutex_lock(g_factory->lock);

    void* creator = ssn_hash_table_get(g_factory->creators,
                                       (void*)((uintptr_t)type + 1));

    ssn_mutex_unlock(g_factory->lock);

    return creator != NULL;
}

int ssn_transport_factory_get_supported_types(
    ssn_transport_type_t* types,
    int max_count)
{
    if (!g_factory || !types || max_count <= 0) {
        return 0;
    }

    ssn_mutex_lock(g_factory->lock);

    int count = 0;
    for (int i = 0; i < max_count && i <= SSN_TRANSPORT_DTLS; i++) {
        void* creator = ssn_hash_table_get(g_factory->creators,
                                           (void*)((uintptr_t)i + 1));
        if (creator) {
            types[count++] = (ssn_transport_type_t)i;
        }
    }

    ssn_mutex_unlock(g_factory->lock);

    return count;
}

ssn_transport_t* ssn_transport_create(ssn_transport_type_t type,
                                       const ssn_transport_config_t* config)
{
    return ssn_transport_factory_create(type, config);
}

void ssn_transport_destroy(ssn_transport_t* transport)
{
    if (!transport) {
        return;
    }

    if (transport->ops.destroy) {
        transport->ops.destroy(transport);
    }

    free(transport);
}

bool ssn_transport_bind(ssn_transport_t* transport,
                        const ssn_address_t* addr)
{
    if (!transport || !addr) {
        LOG_ERROR("Invalid arguments for ssn_transport_bind");
        return false;
    }

    if (!transport->ops.bind) {
        LOG_ERROR("Transport does not support bind");
        return false;
    }

    return transport->ops.bind(transport, addr);
}

bool ssn_transport_connect(ssn_transport_t* transport,
                           const ssn_address_t* addr,
                           int timeout_ms)
{
    if (!transport || !addr) {
        LOG_ERROR("Invalid arguments for ssn_transport_connect");
        return false;
    }

    if (!transport->ops.connect) {
        LOG_ERROR("Transport does not support connect");
        return false;
    }

    return transport->ops.connect(transport, addr, timeout_ms);
}

bool ssn_transport_disconnect(ssn_transport_t* transport)
{
    if (!transport) {
        return false;
    }

    if (!transport->ops.disconnect) {
        return false;
    }

    return transport->ops.disconnect(transport);
}

bool ssn_transport_is_connected(const ssn_transport_t* transport)
{
    if (!transport) {
        return false;
    }

    if (!transport->ops.is_connected) {
        return false;
    }

    return transport->ops.is_connected(transport);
}

int ssn_transport_send(ssn_transport_t* transport,
                       const void* data,
                       size_t len)
{
    if (!transport) {
        return -1;
    }

    if (!transport->ops.send) {
        LOG_ERROR("Transport does not support send");
        return -1;
    }

    return transport->ops.send(transport, data, len);
}

int ssn_transport_recv(ssn_transport_t* transport,
                       void* buffer,
                       size_t size,
                       int timeout_ms)
{
    if (!transport) {
        return -1;
    }

    if (!transport->ops.recv) {
        LOG_ERROR("Transport does not support recv");
        return -1;
    }

    return transport->ops.recv(transport, buffer, size, timeout_ms);
}

bool ssn_transport_listen(ssn_transport_t* transport, int backlog)
{
    if (!transport) {
        return false;
    }

    if (!transport->ops.listen) {
        LOG_ERROR("Transport does not support listen");
        return false;
    }

    return transport->ops.listen(transport, backlog);
}

ssn_transport_t* ssn_transport_accept(ssn_transport_t* transport,
                                       ssn_address_t* client_addr,
                                       int timeout_ms)
{
    if (!transport) {
        return NULL;
    }

    if (!transport->ops.accept) {
        LOG_ERROR("Transport does not support accept");
        return NULL;
    }

    return transport->ops.accept(transport, client_addr, timeout_ms);
}

bool ssn_transport_set_option(ssn_transport_t* transport,
                               int option,
                               const void* value)
{
    if (!transport) {
        return false;
    }

    if (!transport->ops.set_option) {
        return false;
    }

    return transport->ops.set_option(transport, option, value);
}

bool ssn_transport_get_option(const ssn_transport_t* transport,
                              int option,
                              void* value)
{
    if (!transport) {
        return false;
    }

    if (!transport->ops.get_option) {
        return false;
    }

    return transport->ops.get_option(transport, option, value);
}

bool ssn_transport_get_stats(const ssn_transport_t* transport,
                             ssn_transport_stats_t* stats)
{
    if (!transport) {
        return false;
    }

    if (!transport->ops.get_stats) {
        return false;
    }

    return transport->ops.get_stats(transport, stats);
}

bool ssn_transport_get_address(const ssn_transport_t* transport,
                               ssn_address_t* addr)
{
    if (!transport) {
        return false;
    }

    if (!transport->ops.get_address) {
        return false;
    }

    return transport->ops.get_address(transport, addr);
}

