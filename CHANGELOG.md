# Changelog

All notable changes to the ssn (cd-ipc-ssn) IPC framework.

## [2.3.1] - 2026-08-06

### Fixed
- `ssn_node_destroy` 对 ACTIVE 节点直接销毁时的持锁自锁（回归测试）
- IPv6 地址 snprintf 截断警告（超长地址返回错误）
- examples：error_handling/timeout 超时测试断言改为基于回调结果
- UDP 传输标注「不支持 server 模式握手」限制（源码/设计文档/示例 README）
- ssn_client_poll 毫秒超时换算错误（poll(500) 实际仅等待 500ns，回归测试）
- 客户端定时器线程空列表时不再退出（pending 超时机制可靠，回归测试）

### Changed
- 删除无引用死符号 `ssn_ecode_version`；`struct ipc_node` 内部标签统一为 `ssn_node`
- 补写 02 需求分析（含 DDS 对标需求映射）与 05 部署手册；修复 superpowers 文档坏链

## [2.3.0] - 2026-05-07

### Added
- **driver-sdk Phase 3: Architecture Upgrades**
  - `CollectionScheduler`: unified scan scheduler with priority queues and scan groups for shared-bus devices (e.g., RS-485), replaces naive `sleep(3)` in main loop
  - `DataPipeline`: ring buffer + reporter thread between collection and IPC send; supports store-and-forward for disconnected scenarios
  - `DeviceStateMachine`: explicit `DeviceState` enum (Disconnected→Connecting→Connected→Idle→Error) with exponential backoff reconnection
  - `DiagnosticsCollector`: atomic counters per-device and per-driver (connects, collections, IPC sends, errors, latency); configurable periodic log output
- **driver-sdk Phase 2: Feature Completeness**
  - XML config parsing via Poco DOM (previously empty stub, config was SQLite-only)
  - Resource ownership clarified: `shared_ptr<CDevice>` throughout `CDriver` and `ResourceManager`
- **driver-sdk Phase 1: Stability Fixes**
  - `std::atomic<bool>` for main loop stop flag (was plain `bool` — UB at high optimization)
  - `ipc_mutex_` protects concurrent `obj_mapper_`/`client_handle_` access in `UpdateTagsData`
  - Designated-initializer UB in `ssn_url_ref_t` fixed (moved to constructor init list)
  - `CDevice::DestroyTimers()` implemented; `CUserTimer` destructor now calls `vsoa_timer_stop`/`vsoa_timer_delete`
  - `drv_destroy_timer` implemented; `drv_settagdata_text` boundary checks added
- **Unit tests**: 4 new test suites (diagnostics, state machine, data pipeline, scheduler) with 36 tests

### Changed
- `CDriver::devices_` migrated from raw `CDevice*` to `std::shared_ptr<CDevice>`
- `CMainTask` main loop uses `CollectionScheduler::Tick()` for dynamic-interval sleep
- `CDevice::SetDeviceConnected()` now drives state machine transitions
- `PocoXML` and `PocoFoundation` added to link list

### Fixed
- `new[]`/`free` allocator mismatch risk in `ConfigLoader::FreeDrivers` (changed to `delete[]`)
- `conn_type` in `InitDeviceInterface` now correctly parsed from string to int

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
