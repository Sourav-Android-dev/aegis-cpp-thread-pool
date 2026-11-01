#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <thread>

namespace aegis {

/**
 * @brief Behavior when the thread pool shuts down.
 */
enum class ShutdownPolicy {
    GracefulDrain,   ///< Complete all tasks currently enqueued before stopping
    ImmediateCancel  ///< Drop enqueued pending tasks and stop workers after their current task
};

/**
 * @brief Behavior when task submission exceeds bounded queue capacity.
 */
enum class OverloadPolicy {
    Block,         ///< Caller blocks until queue has available capacity
    Reject,        ///< Submission throws aegis::queue_overflow_exception
    DiscardOldest  ///< Drops the oldest low/normal priority pending task
};

/**
 * @brief Comprehensive configuration parameters for the Aegis thread pool.
 */
struct ThreadPoolConfig {
    /// Number of worker threads. Default: hardware concurrency (min 1).
    size_t worker_count =
        (std::thread::hardware_concurrency() > 0) ? std::thread::hardware_concurrency() : 4;

    /// Maximum number of queued tasks. 0 denotes unbounded queue.
    size_t queue_capacity = 0;

    /// Policy when queue_capacity is exceeded.
    OverloadPolicy overload_policy = OverloadPolicy::Block;

    /// Shutdown policy when pool is destroyed or shutdown() called.
    ShutdownPolicy shutdown_policy = ShutdownPolicy::GracefulDrain;

    /// Enable work-stealing deque per worker for reduced contention.
    bool enable_work_stealing = false;

    /// Aging threshold for starvation mitigation in priority scheduling.
    /// If a task waits longer than this, its effective priority increases.
    std::chrono::milliseconds starvation_aging_threshold{200};

    /// Diagnostic name for logging and metrics identification.
    std::string pool_name = "AegisPool";
};

}  // namespace aegis
