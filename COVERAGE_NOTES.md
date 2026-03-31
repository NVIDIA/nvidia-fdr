# Unit Test Coverage Notes — nvidia-fdr

> **Note:** This file is a summary. For detailed tracking, see:
> - [`BLOCKED_TESTS.md`](BLOCKED_TESTS.md) — tests that cannot be generated (missing mocks / unmockable deps)
> - [`FAILING_TESTS.md`](FAILING_TESTS.md) — disabled or known-failing tests with explanations

---

## Coverage Summary

| Metric | Estimated |
|--------|-----------|
| Line coverage | ~60-70% |
| Branch coverage | ~45-55% |
| Function coverage | ~80%+ |
| Test binaries | 10 |
| Total test cases | ~95 |
| Disabled tests | 0 |

---

## Test Files

| Test File | Production Files Covered | Tests |
|-----------|--------------------------|-------|
| `property_variant_test.cpp` | `property_variant.hpp` | 17 |
| `fdr_policy_test.cpp` | `fdr_policy.hpp` | 22 |
| `fdr_common_test.cpp` | `fdr_common.cpp/.hpp` | 24 |
| `ppf_sanity_test.cpp` | `ppf_sanity.cpp` | 8 |
| `fdr_store_test.cpp` | `fdr_store.cpp` | 11 |
| `fdr_http_test.cpp` | `fdr_http.cpp` | 10 |
| `fdr_log_test.cpp` | `fdr_log.cpp` | 5 |
| `fdr_redfish_test.cpp` | `fdr_redfish.cpp` | 7 |
| `fdr_record_test.cpp` | `fdr_record.cpp` | 12 |

---

## Test Infrastructure

| Component | Location | Purpose |
|-----------|----------|---------|
| `testCommon.hpp` | `tests/testCommon.hpp` | Shared header: gtest/gmock, `#define private public` |
| `mockDbusAccessor.hpp` | `tests/mockDbusAccessor.hpp` | `IDbusAccessor` interface + `MockDbusAccessor` with `MOCK_METHOD` |
| `test_stubs.cpp` | `tests/test_stubs.cpp` | No-op stubs for `rfEvent`, `EventSignalHandler`, extern globals |
| `test_record_stubs.cpp` | `tests/test_record_stubs.cpp` | No-op stubs for global `fdr`, `subscribedRecListMap`, `dbus::` accessors |
| `gcovr.cfg` | `gcovr.cfg` | Coverage config with `exclude-throw-branches`, `decisions` |

---

## Running Tests

```bash
# Quick: build + run tests (Docker)
./coverage/run_unit_tests.sh --tests-only

# Full: build + run + coverage report (Docker)
./coverage/run_unit_tests.sh

# Interactive: shell inside Docker container
./coverage/run_unit_tests.sh --shell
```
