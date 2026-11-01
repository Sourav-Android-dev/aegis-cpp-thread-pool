# Aegis Performance Engineering & Benchmark Analysis

This document details the architectural performance characteristics, benchmarking methodology, and scaling observations of the Aegis engine.

---

## 1. Benchmarking Methodology

All benchmarks are built using **Google Benchmark (v1.8+)** compiled with `-O3` optimizations on standard Linux x86_64 hardware.
- Real measurements only: No synthetic or fabricated numbers.
- Dynamic hardware detection: Scaling benchmarks sweep worker thread count from 1 to 8 threads.
- Multi-producer contention test: Measures throughput degradation as producer threads increase from 1 to 16 concurrent submitters.

---

## 2. Microbenchmarks Overview

### 2.1 Sequential Baseline (`sequential_benchmark`)
Evaluates raw CPU arithmetic compute throughput (trigonometric transformations across vectors from $1,000$ to $64,000$ elements) without thread synchronization overhead.

### 2.2 Thread Pool Scaling (`thread_pool_benchmark`)
Measures throughput when submitting 10,000 computation tasks across varying worker counts ($1 \le N_{\text{workers}} \le 8$).
- Demonstrates near-linear scaling for compute-intensive workloads until hardware core saturation.

### 2.3 Contention Benchmark (`contention_benchmark`)
Evaluates queue synchronization overhead when 1, 2, 4, 8, and 16 producer threads concurrently submit tasks into the pool.
- Validates the advantages of batched task submission (`submit_batch()`) and work-stealing queues over single global lock access.

---

## 3. CPU-Bound vs. I/O-Bound Workload Dynamics

### CPU-Bound Tasks
- **Optimal Worker Count**: Set to $N_{\text{hardware\_threads}}$ (or $N - 1$ to leave one core for the OS and scheduler).
- **Bottlenecks**: Cache thrashing and context switching.
- **Aegis Mitigation**: LIFO task picking in work-stealing mode keeps warm cache lines in L1/L2 caches.

### I/O-Bound Tasks (Disk, Network, DB)
- **Optimal Worker Count**: Can be scaled to $2 \times N$ or $4 \times N$ because threads frequently enter sleep/wait states on kernel epoll or read descriptors.
- **Bottlenecks**: Thread stack memory and OS scheduler overhead.
- **Aegis Mitigation**: Worker threads use condition variable parking instead of busy-spin loops.

---

## 4. Key Performance Optimizations in Aegis

1. **Move-Only Functor Type Erasure (`MoveOnlyFunction`)**:
   Eliminates heap allocation for callable wrappers when combined with small-buffer optimization, and supports move-only captures (`std::unique_ptr`, `std::packaged_task`).
2. **Discrete Priority Buckets**:
   Instead of a single $O(\log N)$ heap that locks the entire structure for node balancing, Aegis uses 4 $O(1)$ priority deques, significantly reducing lock hold time during enqueue/dequeue.
3. **Lock-Free Relaxed Telemetry**:
   Performance counters use `std::memory_order_relaxed`, avoiding expensive multi-core cache line bouncing.
