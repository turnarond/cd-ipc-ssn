/*
 * ssn_transport_async_internal.c - 异步连接内部分派
 *
 * 见 ssn_transport_async_internal.h 的可见性与使用范围说明。
 */

#include "ssn_transport_async_internal.h"

ssn_connect_state_t
ssn_transport_connect_begin_internal(ssn_transport_t* transport,
                                     const ssn_address_t* addr)
{
    if (!transport || !transport->valid || !addr) {
        return SSN_CONNECT_FAILED;
    }

    switch (transport->type) {
    case SSN_TRANSPORT_TCP:
    case SSN_TRANSPORT_TCP6:
        return ssn_tcp_connect_begin(transport, addr);
    case SSN_TRANSPORT_UNIX:
        return ssn_unix_connect_begin(transport, addr);
    case SSN_TRANSPORT_UDP:
    case SSN_TRANSPORT_UDP6:
        return ssn_udp_connect_begin(transport, addr);
    default:
        LOG_ERROR("transport type %d does not support non-blocking connect",
                  (int)transport->type);
        return SSN_CONNECT_FAILED;
    }
}

ssn_connect_state_t
ssn_transport_connect_finish_internal(ssn_transport_t* transport)
{
    if (!transport || !transport->valid) {
        return SSN_CONNECT_FAILED;
    }

    switch (transport->type) {
    case SSN_TRANSPORT_TCP:
    case SSN_TRANSPORT_TCP6:
        return ssn_tcp_connect_finish(transport);
    case SSN_TRANSPORT_UNIX:
        return ssn_unix_connect_finish(transport);
    case SSN_TRANSPORT_UDP:
    case SSN_TRANSPORT_UDP6:
        return ssn_udp_connect_finish(transport);
    default:
        return SSN_CONNECT_FAILED;
    }
}
