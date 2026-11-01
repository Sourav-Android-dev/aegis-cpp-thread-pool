#include "aegis/thread_pool.hpp"

namespace aegis {

ThreadPool::ThreadPool(ThreadPoolConfig config)
    : config_(std::move(config)),
      queue_(config_.queue_capacity, config_.overload_policy, config_.starvation_aging_threshold) {
    metrics_.set_worker_counts(static_cast<uint32_t>(config_.worker_count));
    initialize_workers(config_.worker_count);
    AEGIS_LOG_INFO("ThreadPool '", config_.pool_name, "' initialized with ", config_.worker_count,
                   " workers.");
}

ThreadPool::ThreadPool(size_t worker_count)
    : ThreadPool([worker_count]() {
          ThreadPoolConfig cfg;
          cfg.worker_count = worker_count;
          return cfg;
      }()) {}

ThreadPool::~ThreadPool() {
    if (is_running_.load(std::memory_order_relaxed)) {
        if (config_.shutdown_policy == ShutdownPolicy::ImmediateCancel) {
            shutdown_immediate();
        } else {
            shutdown();
        }
    }
}

void ThreadPool::initialize_workers(size_t count) {
    std::lock_guard<std::mutex> lock(workers_mutex_);
    workers_.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        auto worker = std::make_shared<Worker>(static_cast<Worker::WorkerId>(i + 1), queue_,
                                               metrics_, config_.enable_work_stealing);
        workers_.push_back(worker);
    }

    if (config_.enable_work_stealing) {
        for (auto& w : workers_) {
            w->set_peers(workers_);
        }
    }

    for (auto& w : workers_) {
        w->start();
    }
}

bool ThreadPool::cancel(TaskId id) {
    std::shared_lock<std::shared_mutex> lock(tasks_map_mutex_);
    auto it = tasks_metadata_.find(id);
    if (it == tasks_metadata_.end()) {
        return false;
    }

    auto& meta = it->second;
    TaskStatus current_status = meta->status.load(std::memory_order_relaxed);
    if (current_status == TaskStatus::Completed || current_status == TaskStatus::Failed ||
        current_status == TaskStatus::Cancelled) {
        return false;
    }

    meta->cancellation_flag->store(true, std::memory_order_relaxed);
    meta->status.store(TaskStatus::Cancelled, std::memory_order_relaxed);
    metrics_.record_task_cancelled();
    return true;
}

std::optional<TaskStatus> ThreadPool::get_task_status(TaskId id) const {
    std::shared_lock<std::shared_mutex> lock(tasks_map_mutex_);
    auto it = tasks_metadata_.find(id);
    if (it != tasks_metadata_.end()) {
        return it->second->status.load(std::memory_order_relaxed);
    }
    return std::nullopt;
}

void ThreadPool::shutdown() {
    bool expected = true;
    if (!is_running_.compare_exchange_strong(expected, false)) {
        return;
    }

    AEGIS_LOG_INFO("ThreadPool '", config_.pool_name, "' initiating graceful drain shutdown...");
    queue_.close(false);  // Drain remaining tasks

    std::vector<std::shared_ptr<Worker>> workers_copy;
    {
        std::lock_guard<std::mutex> lock(workers_mutex_);
        workers_copy = workers_;
    }

    for (auto& worker : workers_copy) {
        worker->join();
    }

    AEGIS_LOG_INFO("ThreadPool '", config_.pool_name, "' shut down successfully.");
}

void ThreadPool::shutdown_immediate() {
    bool expected = true;
    if (!is_running_.compare_exchange_strong(expected, false)) {
        return;
    }

    AEGIS_LOG_INFO("ThreadPool '", config_.pool_name,
                   "' initiating immediate cancellation shutdown...");
    queue_.close(true);  // Discard pending tasks

    std::vector<std::shared_ptr<Worker>> workers_copy;
    {
        std::lock_guard<std::mutex> lock(workers_mutex_);
        workers_copy = workers_;
    }

    for (auto& worker : workers_copy) {
        worker->stop();
    }

    for (auto& worker : workers_copy) {
        worker->join();
    }

    AEGIS_LOG_INFO("ThreadPool '", config_.pool_name, "' immediately stopped.");
}

void ThreadPool::wait_for_all() {
    while (true) {
        if (queue_.empty() && active_worker_count() == 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
}

void ThreadPool::resize(size_t new_worker_count) {
    if (new_worker_count == 0) {
        throw std::invalid_argument("Worker count must be greater than zero.");
    }

    std::lock_guard<std::mutex> lock(workers_mutex_);
    size_t current_count = workers_.size();

    if (new_worker_count > current_count) {
        for (size_t i = current_count; i < new_worker_count; ++i) {
            auto worker = std::make_shared<Worker>(static_cast<Worker::WorkerId>(i + 1), queue_,
                                                   metrics_, config_.enable_work_stealing);
            workers_.push_back(worker);
            worker->start();
        }
    } else if (new_worker_count < current_count) {
        size_t to_remove = current_count - new_worker_count;
        for (size_t i = 0; i < to_remove; ++i) {
            auto worker = workers_.back();
            workers_.pop_back();
            worker->stop();
            worker->join();
        }
    }

    metrics_.set_worker_counts(static_cast<uint32_t>(workers_.size()));

    if (config_.enable_work_stealing) {
        for (auto& w : workers_) {
            w->set_peers(workers_);
        }
    }
}

bool ThreadPool::is_running() const noexcept {
    return is_running_.load(std::memory_order_relaxed);
}

size_t ThreadPool::worker_count() const noexcept {
    std::lock_guard<std::mutex> lock(workers_mutex_);
    return workers_.size();
}

size_t ThreadPool::active_worker_count() const noexcept {
    std::lock_guard<std::mutex> lock(workers_mutex_);
    size_t active = 0;
    for (const auto& w : workers_) {
        if (!w->is_idle() && w->state() == WorkerState::Running) {
            ++active;
        }
    }
    return active;
}

size_t ThreadPool::idle_worker_count() const noexcept {
    std::lock_guard<std::mutex> lock(workers_mutex_);
    size_t idle = 0;
    for (const auto& w : workers_) {
        if (w->is_idle()) {
            ++idle;
        }
    }
    return idle;
}

size_t ThreadPool::pending_task_count() const noexcept {
    return queue_.size();
}

MetricsSnapshot ThreadPool::metrics() const {
    return metrics_.snapshot();
}

}  // namespace aegis
