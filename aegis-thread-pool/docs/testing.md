# Aegis Verification, Testing & QA Strategy

Aegis incorporates a rigorous testing strategy utilizing **GoogleTest**, addressing edge cases, thread safety, deadlock avoidance, and sustained load.

---

## 1. Test Suite Coverage

| Test File | Target Domain | Key Test Scenarios |
| :--- | :--- | :--- |
| `thread_pool_tests.cpp` | Core API | - Void task execution<br>- Return types (`int`, `std::string`, `double`)<br>- Exception propagation via `std::future`<br>- Batch submission (`submit_batch`)<br>- Task cancellation before execution<br>- Bounded queue overflow (`Reject` policy) |
| `scheduler_tests.cpp` | Scheduling & Timers | - Relative delay task dispatch (`submit_delayed`)<br>- Strict priority queue ordering (Critical -> High -> Normal -> Low)<br>- Task dependency DAG execution |
| `concurrency_tests.cpp` | Multi-Thread Safety | - Concurrent task submission across 8 producer threads<br>- Work-stealing pool validation<br>- Dynamic worker resizing (scale-up and scale-down) |
| `shutdown_tests.cpp` | Lifecycle Reliability | - Graceful drain completing all queued items<br>- Immediate cancel stopping pending tasks<br>- Submission after shutdown rejection |
| `stress_tests.cpp` | High-Volume Stress | - 100,000 tasks concurrently queued across 8 producer threads<br>- Validates atomic counter consistency (0 lost or duplicated tasks) |

---

## 2. Sanitizer Verification Options

Aegis CMake build system provides first-class support for Clang / GCC sanitizers:

### ThreadSanitizer (TSan)
Detects data races, thread leaks, and lock order inversions.
```bash
cmake -S aegis-thread-pool -B build/tsan -DAEGIS_ENABLE_TSAN=ON -DAEGIS_BUILD_TESTS=ON
cmake --build build/tsan --target aegis_tests
./build/tsan/aegis_tests
```

### AddressSanitizer & UndefinedBehaviorSanitizer (ASan + UBSan)
Detects out-of-bounds accesses, use-after-free, memory leaks, and integer overflows.
```bash
cmake -S aegis-thread-pool -B build/asan -DAEGIS_ENABLE_ASAN=ON -DAEGIS_ENABLE_UBSAN=ON -DAEGIS_BUILD_TESTS=ON
cmake --build build/asan --target aegis_tests
./build/asan/aegis_tests
```

---

## 3. Clang-Format and Clang-Tidy

```bash
# Code formatting
clang-format -i include/aegis/*.hpp src/*.cpp examples/*.cpp tests/*.cpp apps/*.cpp

# Static analysis
clang-tidy -p build/default src/*.cpp -- -Iinclude
```
