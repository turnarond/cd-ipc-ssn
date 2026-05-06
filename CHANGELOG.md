# Changelog

All notable changes to the ssn (cd-ipc-ssn) IPC framework.

## [2.2.0] - 2026-05-06

### Added
- Server API functional test (`example_server`, 4 tests): create/destroy, start/stop, add RPC method, echo
- Client API functional test (`example_client`, 5 tests): create/destroy, connect/disconnect, RPC call, subscribe, send message
- Self-contained test infrastructure (embedded server thread, no external setup required)

### Changed
- `ssn_cliauto`: updated enum `ipc_client_auto_state_t` → `ssn_client_auto_state_t`
- `ssn_cliauto`: subscribe now uses `ssn_client_subscribe` instead of `ssn_client_message`
- `ssn_cliauto`: `ssn_client_connect` return type fixed from `int` to `bool`
- `ssn_cliauto.h`: `VSOA` references renamed to `SSN`, `VSOA_CLIENT_AUTO_MAX_PING_LOST` → `SSN_CLIENT_AUTO_MAX_PING_LOST`
- `README.md`: rewritten with current architecture diagram, API examples, full test table
- `CHANGELOG.md` / `VERSION`: added version tracking files
- Documentation: 10+ docs fully migrated from `ipc_`/`cd-ipc-ssn` to `ssn_`/`SSN` naming
- `ssn_version.h`: bumped to 2.2.0

### Fixed
- Client: `ssn_client_process_events` no longer disconnects on `EAGAIN`/`EWOULDBLOCK` (same pattern as server fix in v2.1.0)
- `ssn_frame.c`: log messages `ipc` → `ssn`
- `ssn_cliauto.c`: comments `IPC` → `SSN`, old file name references removed

### Removed
- `src/cd_ipc_client_refactored.c` — obsolete refactoring draft

### Test Results (8 suites, 116 tests)
- test_transport: 55/0 passed
- test_node_basic: 3/3 passed
- test_node: 5/5 passed
- test_protocol: 25/0 passed
- test_protocol_integration: 19/0 passed
- example_server: 4/4 passed
- example_client: 5/5 passed

## [2.1.0] - 2026-04-29

### Added
- Protocol layer receive-side implementation (RPC, PubSub, Message polling)
- Node-level background server poller support for tests
- Complete protocol integration test suite (19 tests)

### Changed
- **Unified naming**: all public types, functions, and macros migrated from `cd_ipc_`/`ipc_` prefix to `ssn_` prefix
  - `ipc_client_t` → `ssn_client_t`, `ipc_server_t` → `ssn_server_t`
  - `ipc_node_t` → `ssn_node_t`, `ipc_header_t` → `ssn_header_t`
  - `IPC_MAX_PACKET_SIZE` → `SSN_MAX_PACKET_SIZE`, etc.
- **Client/Server refactored**: integrated new protocol layer modules (ssn_rpc, ssn_pubsub, ssn_msg)
- `ssn_node_subscribe` now requires `peer_address` parameter for explicit connection targeting
- `ssn_client_set_on_message` now sets both `onmsg` and `onsub` callbacks
- `create_server_address` now includes proper protocol prefix (`tcp://`, `unix://`)
- Error code type renamed from `ssn_error_t` to `ssn_ecode_t` to avoid conflict with transport layer

### Fixed
- Deadlock: `ssn_node_get_client`/`ssn_node_get_server` removed redundant internal locking
- TCP/UDP transports: `get_option` now correctly returns socket fd (was always -1)
- Server: client connections no longer destroyed on `EAGAIN` from non-blocking recv
- Protocol send functions: replaced stack-allocated header with `SSN_MAX_PACKET_SIZE` buffer (fixes SIGSEGV)
- Protocol poll functions: implemented real receive+parse+dispatch logic (were empty stubs)
- `test_protocol_integration`: fixed NULL pointer dereference in callbacks, RPC response routing

### Test Results
- test_transport: 55/0 passed
- test_node_basic: 3/3 passed
- test_node: 5/5 passed
- test_protocol: 25/0 passed
- test_protocol_integration: 19/0 passed

## [2.0.0] - 2026-04-21

### Added
- Node discovery mechanism (multicast, directory service)
- QoS framework (reliability, priority, bandwidth control)
- Node abstraction layer (service registration, topic management)
- Multi-protocol support (Unix Socket, TCP, UDP)
- Example code and documentation

### Known Limitations
- TLS/DTLS secure transport not yet implemented

## [1.0.0] - 2026-04-19

### Added
- Transport layer abstraction (Unix Socket / TCP / UDP)
- Node abstraction layer Phase 1 (create, start, stop, destroy)
- Communication APIs (send message, publish/subscribe, RPC)
- Version management
- Unit tests

### Known Limitations
- Node discovery mechanism (Phase 2 planned)
- QoS support (Phase 3 planned)
- TLS/DTLS secure transport (future version)
