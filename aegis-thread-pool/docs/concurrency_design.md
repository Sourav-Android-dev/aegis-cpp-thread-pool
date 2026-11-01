# Concurrency Design & Thread Model

Aegis enforces a strict, verifiable concurrency design to eliminate data races, prevent deadlocks, and minimize lock contention under heavy multi-producer / multi-consumer load.

---

## 1. Thread Ownership and Lifecycle

```
[Main / Producer Thread(s)]
   |
   +--> submit() -------------------+
                                    | (mutex + CV)
                                    v
                            [ aegis::TaskQueue ]
                                    |
            +-----------------------+-----------------------+
            |                       |                       |
            v                       v                       v
     [ Worker 1 ]             [ Worker 2 ]            [ Worker N ]
     (OS Thread)              (OS Thread)             (OS Thread)
```

- **Thread Joinability Guarantee**: Aegis never detaches threads (`std::thread::detach()` is strictly prohibited across the codebase). Every worker thread is owned by a `Worker` instance and joined either on explicit shutdown or during destruction.
- **Worker States**:
  Workers undergo explicit transitions using relaxed or acquire/release atomics:
  - `Starting`: Thread is spawned and binding to OS core.
  - `Idle`: Worker is blocked waiting on `not_empty_cv_` in the task queue.
  - `Running`: Worker has popped a task and is executing user code.
  - `Stopped`: Worker has terminated its run loop and is waiting for `join()`.

---

## 2. Deadlock Avoidance Strategy

1. **Lock Hierarchy**:
   Locks are strictly partitioned and acquired in a single deterministic direction:
   - Level 1: `ThreadPool::workers_mutex_` (guarantees worker list stability)
   - Level 2: `TaskQueue::mutex_` (guarantees queue integrity)
   - Level 3: `ThreadPool::tasks_map_mutex_` (shared mutex for metadata queries)
   - Rule: A thread holding `TaskQueue::mutex_` never attempts to acquire `workers_mutex_`.
2. **Lock Hold Duration**:
   Mutex locks are never held while invoking user callbacks or unknown callables. Tasks are popped completely out of the queue, locks are released, and only then is the callable invoked on the worker thread.

---

## 3. Starvation Mitigation Engine

In strict priority queues, an endless stream of high-priority tasks will starve normal and low-priority tasks indefinitely. Aegis solves this via **Starvation Aging Mitigation**:

$$\text{Effective Priority} = \text{Base Priority} + \frac{T_{\text{now}} - T_{\text{submission}}}{\tau_{\text{aging}}}$$

- Aegis maintains 4 discrete priority FIFO deques (`Critical = 3`, `High = 2`, `Normal = 1`, `Low = 0`).
- During each worker dequeue attempt, Aegis evaluates the front element of lower priority buckets.
- If the wait duration exceeds `starvation_aging_threshold` (default: 200 ms), the aged task is promoted and dispatched immediately, preventing priority inversion and thread starvation.
