# Blocked Unit Tests

This document tracks tests that cannot be generated due to missing mocks or unmockable
dependencies. Only items that are **currently** blocked are listed here.

---

## Current Status (2026-03-18)

**10 test binaries, ~95 individual tests, 0 DISABLED.**
**Estimated coverage: ~60-70% line · ~45-55% branch · ~80%+ function**

### Test Infrastructure Available

| Component | Location | Purpose |
|-----------|----------|---------|
| `testCommon.hpp` | `tests/testCommon.hpp` | Shared header: `#define private public`, gtest/gmock includes, `using namespace ::testing` |
| `mockDbusAccessor.hpp` | `tests/mockDbusAccessor.hpp` | `IDbusAccessor` interface + `MockDbusAccessor` with `MOCK_METHOD` for all `dbus::` functions |
| `test_stubs.cpp` | `tests/test_stubs.cpp` | No-op stubs for `rfEvent::createLogEntry`, `EventSignalHandler::~EventSignalHandler`, extern globals |
| `test_record_stubs.cpp` | `tests/test_record_stubs.cpp` | No-op stubs for global `fdr` pointer, `FlightDataRecorder_c::subscribedRecListMap`, `dbus::` accessors |
| `gcovr.cfg` | `gcovr.cfg` | Coverage config: excludes tests/subprojects/protobuf, enables branch/decision coverage |

---

## Blocked: Cannot Test Without Production Code Changes

### 1. `fdr.cpp` — `FlightDataRecorder_c` methods

**Blocked by:** Tight coupling to DBus, sdeventplus timers, filesystem, and global state. Constructor requires a live `sdbusplus::bus_t`, real filesystem paths, and running `sd_event` loop.

**To unblock:** Break into smaller classes with dependency injection (DI) for bus, event loop, and filesystem.

---

### 2. `fdr_compact.cpp` — `Compactor()`, `CompactorEngine()`

**Blocked by:** Depends on full `FlightDataRecorder_c` state, real filesystem, and `FDRStore` instances with existing directories.

**To unblock:** Extract compaction logic into a pure-logic class that takes `FDRStore` instances as parameters.

---

### 3. `fdr_timers.cpp` — All timer callbacks

**Blocked by:** Tightly coupled to `sdeventplus::utility::Timer` and global `fdr` pointer.

**To unblock:** Extract timer callback logic into standalone functions that receive dependencies as parameters.

---

### 4. `fdr_events.cpp` — `EventSignalHandler`, `eventParser()`

**Blocked by:** DBus signal registration (`sdbusplus::bus::match_t`), phosphor-logging, global `fdr` pointer.

**To unblock:** Make `EventSignalHandler` accept a bus reference via DI; mock the match registration.

---

### 5. `fdr_grp_update.cpp` — `FdrGrpUpdate::RefreshAndStore`

**Blocked by:** Hard dependency on nvidia-shmem library (`telemetry_mrd_client`), cannot be linked in test environment.

**To unblock:** Abstract shmem access behind an interface.

---

### 6. `fdr_bookoferrors.cpp` — `BookOfErrorEngine`

**Blocked by:** Depends on `fdr` global, `FDRStore`, and filesystem.

**To unblock:** Same DI pattern as `fdr.cpp`.

---

### 7. `dbus_accessor.cpp` — All functions

**Blocked by:** Every function calls `sdbusplus::bus::bus::new_default()` which requires a running D-Bus daemon. Free functions cannot be mocked without a wrapper.

**To unblock:** Wrap in `IDbusAccessor` interface (mock already exists in `tests/mockDbusAccessor.hpp`); change callers to use the interface.

---

### 8. `fdr_redfish.cpp` — `login()`, `logout()`, `query()`, `query_json()`

**Blocked by:** `HttpClient` is a concrete class owned internally by `RedfishClient`. Cannot inject a mock HTTP backend.

**To unblock:** Accept `HttpClient` pointer via constructor DI, or make `HttpClient` inherit from an `IHttpClient` interface.

---

### 9. `fdr_record.cpp` — `Refresh()`, `Store()`, `refreshDataCallback()`

**Blocked by:** `Refresh()` calls `dbus::readDbusProperty()`, `exec()`, and Redfish queries. `Store()` depends on `fdr->rfc` global.

**To unblock:** Inject a `IDbusAccessor` and `ICommandRunner` into `Record`.

---

### 10. `fdr_common.cpp` — System-dependent functions

| Function | Blocked by |
|----------|-----------|
| `exec()` | Calls `popen()` — system-level integration |
| `checkEnvValue()` | Calls `fw_printenv` — hardware-specific |
| `warnFdrLowSpace()` | Calls `rfEvent::createLogEntry` via DBus |
| `get_procuptime()` | Reads `/proc/uptime` |
| `copyFile()`, `BkupAndDeleteCorruptFile()` | Filesystem operations |

**To unblock:** Wrap each behind a mockable interface or use a filesystem abstraction layer.

---

## Reaching 90%+ Coverage

To push beyond ~65-70% line coverage, the following production code changes are needed:

1. **Dependency injection for `HttpClient`** in `RedfishClient`
2. **DBus abstraction layer** — wrap `getBus()` behind `IDbusAccessor` interface
3. **`exec()` abstraction** — wrap `popen()` behind a mockable `ICommandRunner`
4. **Break `fdr.cpp` into smaller, testable units** with injected dependencies
5. **Remove static state** from `ppf_sanity.cpp`'s `UniqueParamChecker`
