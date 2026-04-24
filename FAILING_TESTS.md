# Failing/Disabled Unit Tests

Currently **0 tests** are disabled or failing.

---

## Known Test Limitations

### Network-dependent tests in `fdr_http_test.cpp`

The following tests make real HTTP requests to `http://www.google.com/` and will
fail in offline or Docker environments without internet access:

| Test | Expected behavior |
|------|-------------------|
| `FdrHttp.HTTPrequest` | GET returns 200, POST returns 405 |
| `FdrHttp.HTTPGet` | GET returns 200 |
| `FdrHttp.HTTPPost` | POST returns 405 |

**Mitigation:** These tests pass in environments with internet access. In CI
Docker environments without network, they will fail but do not indicate a code
bug. Consider adding a `GTEST_SKIP()` guard that checks for network
connectivity, or mocking `HttpClient` via DI.

### Static state in `ppf_sanity.cpp`

`UniqueParamChecker` uses `static` local variables which persist across test
cases within the same binary. Tests in `ppf_sanity_test.cpp` use unique
component names to avoid contamination, but adding new tests requires care
to avoid collisions with existing static state.

---

## History

### 2026-03-18 — Initial test suite creation

All 10 test binaries and ~95 tests pass. No disabled tests.

| Phase | Tests added | Files |
|-------|------------|-------|
| Phase 1 | 70 tests | `fdr_common_test`, `property_variant_test`, `fdr_policy_test`, `ppf_sanity_test` |
| Phase 2 | 19 tests | `fdr_store_test`, `fdr_http_test` (extended), `fdr_log_test` |
| Phase 3 | 12 tests | `fdr_redfish_test`, `fdr_record_test` |
