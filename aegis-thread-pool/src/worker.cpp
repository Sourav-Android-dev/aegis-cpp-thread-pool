#include "aegis/worker.hpp"

#include <random>

namespace aegis {

Worker::Worker(WorkerId id, TaskQueue& global_queue, MetricsTracker& metrics,
               bool enable_work_stealing)
    : id_(id),
      global_queue_(global_queue),
      metrics_(metrics),
      enable_work_stealing_(enable_work_stealing) {
    if (enable_work_stealing_) {
        local_queue_ = std::make_unique<WorkStealingQueue>();
    }
}

Worker::~Worker() {
    stop();
    join();
}

void Worker::start() {
    thread_ = std::thread(&Worker::run, this);
}

void Worker::stop() {
    stop_requested_.store(true, std::memory_order_relaxed);
}

void Worker::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Worker::set_peers(const std::vector<std::shared_ptr<Worker>>& peers) {
    peers_.clear();
    for (const auto& p : peers) {
        if (p.get() != this) {
            peers_.push_back(p);
        }
    }
}

std::optional<ExecutableTask> Worker::acquire_task() {
    // 1. Try local work-stealing queue if present
    if (local_queue_) {
        if (auto task = local_queue_->pop()) {
            return task;
        }
    }

    // 2. Try global queue (blocking pop with small timeout if work stealing enabled, or standard
    // blocking pop)
    if (enable_work_stealing_) {
        if (auto task = global_queue_.pop_wait_for(std::chrono::milliseconds{10})) {
            return task;
        }

        // 3. Work stealing from peers
        if (!peers_.empty()) {
            thread_local std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<size_t> dist(0, peers_.size() - 1);
            size_t start_idx = dist(rng);

            for (size_t i = 0; i < peers_.size(); ++i) {
                size_t idx = (start_idx + i) % peers_.size();
                if (auto peer = peers_[idx].lock()) {
                    if (auto peer_queue = peer->local_queue()) {
                        if (auto stolen = peer_queue->steal()) {
                            return stolen;
                        }
                    }
                }
            }
        }

        // Re-try global pop if nothing to steal
        if (global_queue_.is_closed() && global_queue_.empty()) {
            return std::nullopt;
        }
        return global_queue_.pop_wait_for(std::chrono::milliseconds{50});
    }

    // Standard mode: block until global queue has tasks or closes
    return global_queue_.pop();
}

void Worker::run() {
    while (!stop_requested_.load(std::memory_order_relaxed)) {
        state_.store(WorkerState::Idle, std::memory_order_relaxed);

        auto opt_task = acquire_task();
        if (!opt_task.has_value()) {
            if (global_queue_.is_closed() && global_queue_.empty()) {
                break;
            }
            continue;
        }

        auto& task = *opt_task;

        if (task.is_cancelled()) {
            if (task.metadata) {
                task.metadata->status.store(TaskStatus::Cancelled, std::memory_order_relaxed);
            }
            metrics_.record_task_cancelled();
            continue;
        }

        state_.store(WorkerState::Running, std::memory_order_relaxed);
        metrics_.worker_started_task();

        auto now = std::chrono::steady_clock::now();
        std::chrono::nanoseconds wait_time{0};
        if (task.metadata) {
            wait_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                now - task.metadata->submission_time);
        }

        auto start_exec = std::chrono::steady_clock::now();

        // Execute task
        task.func();

        auto end_exec = std::chrono::steady_clock::now();
        auto exec_time =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end_exec - start_exec);

        metrics_.worker_finished_task();
        tasks_executed_.fetch_add(1, std::memory_order_relaxed);

        if (task.metadata &&
            task.metadata->status.load(std::memory_order_relaxed) == TaskStatus::Failed) {
            metrics_.record_task_failed(wait_time, exec_time);
        } else {
            metrics_.record_task_completed(wait_time, exec_time);
        }
    }

    state_.store(WorkerState::Stopped, std::memory_order_relaxed);
}

}  // namespace aegis
