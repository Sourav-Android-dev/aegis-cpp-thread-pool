#include "aegis/scheduler.hpp"

#include <algorithm>

namespace aegis {

Scheduler::Scheduler(ThreadPoolConfig config) : pool_(std::move(config)) {
    dep_monitor_thread_ = std::thread([this]() { check_dependent_tasks(); });
}

Scheduler::Scheduler(size_t worker_count)
    : Scheduler([worker_count]() {
          ThreadPoolConfig cfg;
          cfg.worker_count = worker_count;
          return cfg;
      }()) {}

Scheduler::~Scheduler() {
    shutdown();
}

bool Scheduler::cancel(TaskId id) {
    return pool_.cancel(id);
}

std::optional<TaskStatus> Scheduler::get_status(TaskId id) const {
    return pool_.get_task_status(id);
}

void Scheduler::wait_all() {
    // Wait for dependent tasks to dispatch
    while (true) {
        {
            std::lock_guard<std::mutex> lock(dep_mutex_);
            if (pending_dependents_.empty()) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    pool_.wait_for_all();
}

void Scheduler::shutdown() {
    if (monitor_running_.load(std::memory_order_relaxed)) {
        monitor_running_.store(false, std::memory_order_relaxed);
        if (dep_monitor_thread_.joinable()) {
            dep_monitor_thread_.join();
        }
    }
    pool_.shutdown();
}

void Scheduler::register_dependent_task(std::vector<TaskId> dependencies, Priority priority,
                                        MoveOnlyFunction action) {
    std::lock_guard<std::mutex> lock(dep_mutex_);
    pending_dependents_.push_back(
        DependentTaskNode{std::move(dependencies), priority, std::move(action)});
}

void Scheduler::check_dependent_tasks() {
    while (monitor_running_.load(std::memory_order_relaxed)) {
        std::vector<DependentTaskNode> ready_to_fire;

        {
            std::lock_guard<std::mutex> lock(dep_mutex_);
            auto it = pending_dependents_.begin();
            while (it != pending_dependents_.end()) {
                auto& node = *it;
                bool all_completed = true;

                for (TaskId dep_id : node.remaining_dependencies) {
                    auto status = pool_.get_task_status(dep_id);
                    if (!status.has_value() || *status != TaskStatus::Completed) {
                        all_completed = false;
                        break;
                    }
                }

                if (all_completed) {
                    ready_to_fire.push_back(std::move(node));
                    it = pending_dependents_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        for (auto& ready_node : ready_to_fire) {
            pool_.submit_priority(ready_node.priority, std::move(ready_node.action));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
}

}  // namespace aegis
