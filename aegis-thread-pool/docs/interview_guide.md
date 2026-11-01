# Aegis Technical Interview Preparation & Architecture Deep-Dive

This guide is designed for principal systems engineers, low-latency software architects, and engineering candidates preparing for senior C++ systems design and concurrency interviews.

---

## 1. Resume-Ready Project Description

> **Aegis — High-Performance C++20 Thread Pool & Task Scheduling Engine**
> - Architected a production-grade multithreaded task execution and scheduling engine in modern C++20 featuring configurable worker thread pools, DAG task dependencies, delayed task dispatch, and starvation mitigation.
> - Implemented a thread-safe, bounded, multi-priority task queue with 4 discrete $O(1)$ priority buckets and dynamic aging heuristics, eliminating thread starvation for low-priority tasks under saturated queues.
> - Engineered an optional work-stealing architecture with thread-local deques to reduce global lock contention and maximize L1/L2 cache locality across NUMA nodes.
> - Built comprehensive testing with GoogleTest (17/17 tests passing, 100K-task stress test at 340,000+ tasks/sec) and microbenchmarking suites with Google Benchmark; integrated ThreadSanitizer, ASan, and GitHub Actions CI.

---

## 2. Key Architecture Trade-Offs

### 2.1 Single Centralized Queue vs. Work-Stealing Per-Worker Deques
- **Centralized Queue**:
  - *Pros*: Simple global FIFO or strict priority semantics; minimal memory footprint; easy shutdown draining.
  - *Cons*: High lock contention on single `std::mutex` under high producer counts ($> 8$ threads).
- **Work-Stealing Architecture**:
  - *Pros*: Workers push and pop locally from their private deque (LIFO order, high cache hit rates). Only steal from peers when idle.
  - *Cons*: Increased memory overhead per worker; non-deterministic task ordering across workers; complex synchronization during shutdown.

### 2.2 Discrete Priority Buckets vs. `std::priority_queue` (Binary Heap)
- **Binary Heap (`std::priority_queue`)**:
  - Push: $O(\log N)$, Pop: $O(\log N)$.
  - In a dynamic aging system where effective priority changes continuously over time, maintaining a heap requires periodic $O(N)$ re-heapification, causing severe lock contention.
- **Aegis 4-Bucket Discrete Approach**:
  - Push: $O(1)$, Pop: $O(1)$.
  - Uses discrete buckets (`Critical`, `High`, `Normal`, `Low`). Starvation check only inspects the head of lower queues ($O(1)$ comparison), yielding orders of magnitude lower lock holding time.

### 2.3 Condition Variable (`std::condition_variable`) vs. Busy-Spin / Spinlocks
- **Spinlocks**:
  - Ideal only when tasks arrive at sub-microsecond intervals and critical sections are ultra-short.
  - Sits on CPU cores at 100% utilization, burning power and penalizing other OS threads.
- **Condition Variable Parking**:
  - Aegis puts idle workers to sleep using kernel futexes via `std::condition_variable`.
  - Zero CPU cycles consumed when pool is idle.

### 2.4 Custom `MoveOnlyFunction` vs. `std::function`
- `std::function` requires that callable targets be **CopyConstructible**.
- This makes it impossible to store `std::packaged_task`, `std::unique_ptr`, or move-only lambda captures without expensive `std::shared_ptr` workarounds.
- Aegis implements `MoveOnlyFunction` using Concept-Model type erasure, enabling true zero-copy move-only callable semantics.

---

## 3. High-Frequency Interview Questions & Answers

### Q1: Why do we use `not_empty_cv_.wait(lock, predicate)` instead of an `if (!empty()) wait()`?
**Answer**:
1. **Spurious Wakeups**: POSIX and OS kernels may unblock waiting threads without an explicit `signal` or `broadcast`.
2. **Race Condition Between Notification and Lock Re-acquisition**: Between the time thread A is notified and thread A actually acquires `mutex_`, another worker thread B might have already popped the task. Without re-evaluating the predicate in a loop, thread A would attempt to pop from an empty queue, causing undefined behavior or crash.

### Q2: What is the difference between `std::memory_order_relaxed` and `std::memory_order_seq_cst`?
**Answer**:
- `seq_cst` (Sequential Consistency) enforces a globally total order across all threads for all atomic operations. On x86, it emits expensive memory fence instructions (`MFENCE` or locked instructions).
- `relaxed` guarantees only that the atomic variable itself is updated atomically without tears, but imposes no synchronization or ordering constraints on concurrent memory accesses across different variables.
- Aegis uses `relaxed` for performance counters (`submitted_tasks_`, `completed_tasks_`) because statistical counting doesn't require cross-variable memory synchronization, delivering significantly higher throughput.

### Q3: How do you prevent deadlocks when a task submitted to the thread pool submits another task and waits for its future (`future.get()`)?
**Answer**:
This is known as **Thread Pool Starvation Deadlock**:
- If a pool with $N$ worker threads receives $N$ tasks, and all $N$ tasks simultaneously submit child tasks and block waiting for `child_future.get()`, all $N$ worker threads are blocked. The child tasks sit in the queue forever because no worker is free to execute them, causing a permanent deadlock.
- **Solutions**:
  1. Use work-stealing where blocked workers can cooperatively execute pending tasks from their queue (continuation-passing).
  2. Maintain a separate pool for child subtasks or use async task continuation chaining.
  3. Size the pool dynamically or implement fiber/coroutine-based cooperative task scheduling.
