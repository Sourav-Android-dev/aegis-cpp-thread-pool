# Aegis Architecture & Subsystem Design

Aegis is an enterprise-grade, high-performance C++20 thread pool and task scheduling engine designed for ultra-low latency, thread safety, fault isolation, and deterministic resource lifecycles.

---

## 1. Subsystem Decomposition

```
+-------------------------------------------------------------------------+
|                              Client Layer                               |
|        (submit, submit_priority, submit_delayed, submit_batch)          |
+--------------------+-----------------------------------+----------------+
                     |                                   |
                     v                                   v
+------------------------------------+   +--------------------------------+
|         aegis::ThreadPool          |   |        aegis::Scheduler        |
|    - Worker Lifecycle              |   |    - Dependency Graph (DAG)    |
|    - Thread Management             |   |    - Delayed / Timer Dispatch  |
|    - Graceful / Immediate Shutdown |   |    - Periodic Monitoring       |
+--------------------+---------------+   +---------------+----------------+
                     |                                   |
                     +-----------------+-----------------+
                                       |
                                       v
                     +-----------------------------------+
                     |         aegis::TaskQueue          |
                     |  - 4 Discrete Priority Buckets    |
                     |  - Starvation Aging Mitigation    |
                     |  - Bounded Backpressure Engine    |
                     |  - Delayed Tasks Min-Heap         |
                     +-----------------+-----------------+
                                       |
                     +-----------------+-----------------+
                     |                                   |
                     v                                   v
+------------------------------------+   +--------------------------------+
|          Worker Threads            |   |     Work-Stealing Modules      |
|  - Cache-Friendly Deques           |   |  - Local LIFO Pop              |
|  - Atomic State Transitions        |   |  - Remote FIFO Steal           |
|  - Exception & Failure Boundary    |   |  - Reduced Global Contention   |
+--------------------+---------------+   +--------------------------------+
                     |
                     v
+-------------------------------------------------------------------------+
|                   MetricsTracker & Observability                        |
|   - Atomics (Submitted, Completed, Failed, Cancelled, Pending)          |
|   - Nanosecond Latency & Wait Timers, Throughput Calculations           |
|   - Thread-Safe Structured Logger with Configurable Sinks               |
+-------------------------------------------------------------------------+
```

---

## 2. Core Components

### 2.1 ThreadPool (`aegis::ThreadPool`)
- Acts as the central coordinating façade.
- Owns the worker threads and the primary `TaskQueue`.
- Supports dynamic resizing of worker threads (`resize()`) without restarting or dropping pending tasks.
- Manages task futures and promises, returning `std::future<R>` to callers for asynchronous result retrieval and exception re-throwing.

### 2.2 Task Queue (`aegis::TaskQueue`)
- Implements bounded capacity with configurable overload policies:
  - `OverloadPolicy::Block`: Producer blocks on condition variable `not_full_cv_`.
  - `OverloadPolicy::Reject`: Producer immediately receives `QueueOverflowException`.
  - `OverloadPolicy::DiscardOldest`: Drops the oldest low-priority task, notifying cancelled listeners.
- Implements **4-tier priority bucketing** (`Critical`, `High`, `Normal`, `Low`) combined with **Starvation Aging Mitigation**.
- Automatically delays execution for tasks submitted with relative timeouts until their `due_time`.

### 2.3 Task Representation & Move-Only Functor (`aegis::MoveOnlyFunction`)
- Standard `std::function` requires copy-constructible closures, which precludes move-only types like `std::packaged_task`, `std::unique_ptr`, and modern C++ move-only lambda captures.
- Aegis implements `MoveOnlyFunction`, a high-performance type-erased callable pattern based on the Model-Concept design, allowing zero-overhead moves and arbitrary callable execution.

### 2.4 Worker (`aegis::Worker`)
- Encapsulates a persistent OS thread (`std::thread`).
- Transitions atomically across lifecycle states: `Starting` -> `Idle` <-> `Running` -> `Stopped`.
- Executes inside an exception boundary ensuring that uncaught user task exceptions do not terminate the worker thread.

### 2.5 Work-Stealing Extension (`aegis::WorkStealingQueue`)
- When enabled in `ThreadPoolConfig.enable_work_stealing`, each worker thread maintains its private double-ended queue.
- Worker pops and pushes tasks at the bottom (LIFO order, maximizing CPU L1/L2 cache locality).
- When a worker's local deque and the global queue are empty, it attempts to steal tasks from the top (FIFO order) of randomly selected peer worker deques.

---

## 3. Reliability & Lifecycle Guarantees

1. **Deterministic Shutdown**:
   - `ShutdownPolicy::GracefulDrain`: Worker threads will not terminate until all currently pending tasks in the queue are processed to completion.
   - `ShutdownPolicy::ImmediateCancel`: Remaining pending tasks are atomically marked `Cancelled`, their promises resolved or broken, and workers exit once their active task finishes.
2. **Safe Destruction**:
   - Destructor always joins all worker threads. No detached threads are ever spawned.
