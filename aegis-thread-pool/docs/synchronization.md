# Synchronization Invariants & Memory Model

This document outlines the memory ordering, synchronization primitives, and correctness invariants enforced in Aegis.

---

## 1. Concurrency Invariants

| Component | Invariant | Synchronization Mechanism |
| :--- | :--- | :--- |
| `TaskQueue` | Task count equals sum of priority buckets and delayed tasks. | `std::mutex` + `std::condition_variable` |
| `Worker` | State transition from `Idle` to `Running` strictly occurs after successfully claiming task. | Atomic state + `std::memory_order_relaxed` / `acquire` |
| `MetricsTracker` | Counter increments are linearizable and thread-safe without coarse locking. | `std::atomic<uint64_t>` fetch operations |
| `TaskMetadata` | Cancellation flag transitions at most once from `false` to `true`. | `std::atomic<bool>` CAS / exchange |
| `ThreadPool` | Worker vector modifications are mutually exclusive. | `std::mutex workers_mutex_` |
| `Scheduler` | Task status read-heavy access does not block worker threads. | `std::shared_mutex tasks_map_mutex_` (Reader-Writer) |

---

## 2. Memory Ordering Rationale

### Relaxed Ordering (`std::memory_order_relaxed`)
- Used for telemetry metrics counters (`submitted_tasks_`, `completed_tasks_`, `failed_tasks_`, `total_exec_time_ns_`).
- **Rationale**: Strict sequential consistency is not required for statistical counters across independent cores. Relaxed atomic operations avoid CPU cache coherency bus penalties while maintaining atomic read-modify-write safety.

### Acquire-Release Ordering (`std::memory_order_acquire` / `release`)
- Used for worker shutdown signaling (`is_running_`, `closed_`).
- **Rationale**: When a producer closes a queue, all memory modifications prior to the close operation must become visible to the worker thread when it observes `closed_ == true`.

---

## 3. Condition Variable Spurious Wakeup Guarding

All wait operations on `std::condition_variable` use the predicate overload or an explicit `while` loop:

```cpp
not_empty_cv_.wait(lock, [this]() {
    return closed_ || ready_count_ > 0 || !delayed_queue_.empty();
});
```

This guarantees safety against OS-level spurious wakeups and guarantees workers do not attempt dequeue from an empty state.
