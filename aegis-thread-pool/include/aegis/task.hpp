#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "aegis/task_priority.hpp"
#include "aegis/task_status.hpp"

namespace aegis {

using TaskId = uint64_t;
inline constexpr TaskId INVALID_TASK_ID = 0;

/**
 * @brief Move-only type-erased callable abstraction supporting move-only captures.
 */
class MoveOnlyFunction {
private:
    struct Concept {
        virtual ~Concept() = default;
        virtual void invoke() = 0;
    };

    template <typename F>
    struct Model final : Concept {
        F callable_;
        explicit Model(F&& f) : callable_(std::forward<F>(f)) {}
        void invoke() override { callable_(); }
    };

    std::unique_ptr<Concept> impl_;

public:
    MoveOnlyFunction() noexcept = default;
    ~MoveOnlyFunction() = default;

    MoveOnlyFunction(MoveOnlyFunction&&) noexcept = default;
    MoveOnlyFunction& operator=(MoveOnlyFunction&&) noexcept = default;

    MoveOnlyFunction(const MoveOnlyFunction&) = delete;
    MoveOnlyFunction& operator=(const MoveOnlyFunction&) = delete;

    template <typename F>
    requires(!std::is_same_v<std::decay_t<F>, MoveOnlyFunction> &&
             std::is_invocable_v<std::decay_t<F>>) MoveOnlyFunction(F&& f)
        : impl_(std::make_unique<Model<std::decay_t<F>>>(std::forward<F>(f))) {}

    void operator()() const {
        if (impl_) {
            impl_->invoke();
        }
    }

    explicit operator bool() const noexcept { return impl_ != nullptr; }
};

/**
 * @brief Complete metadata and tracking envelope for a scheduled task.
 */
struct TaskMetadata {
    TaskId id{INVALID_TASK_ID};
    std::string name;
    Priority priority{Priority::Normal};
    std::atomic<TaskStatus> status{TaskStatus::Pending};
    std::chrono::steady_clock::time_point submission_time;
    std::chrono::steady_clock::time_point scheduled_time;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;

    std::shared_ptr<std::atomic<bool>> cancellation_flag;

    TaskMetadata()
        : submission_time(std::chrono::steady_clock::now()),
          scheduled_time(submission_time),
          cancellation_flag(std::make_shared<std::atomic<bool>>(false)) {}
};

/**
 * @brief Internal executable task bundle containing callable, metadata, and aging metrics.
 */
struct ExecutableTask {
    TaskId id{INVALID_TASK_ID};
    std::shared_ptr<TaskMetadata> metadata;
    MoveOnlyFunction func;

    // Time when task becomes eligible for dispatch (default = submission time)
    std::chrono::steady_clock::time_point due_time;

    ExecutableTask() = default;
    ExecutableTask(TaskId tid, std::shared_ptr<TaskMetadata> meta, MoveOnlyFunction f,
                   std::chrono::steady_clock::time_point due = std::chrono::steady_clock::now())
        : id(tid), metadata(std::move(meta)), func(std::move(f)), due_time(due) {}

    ExecutableTask(ExecutableTask&&) noexcept = default;
    ExecutableTask& operator=(ExecutableTask&&) noexcept = default;
    ExecutableTask(const ExecutableTask&) = delete;
    ExecutableTask& operator=(const ExecutableTask&) = delete;

    [[nodiscard]] bool is_ready(std::chrono::steady_clock::time_point now =
                                    std::chrono::steady_clock::now()) const noexcept {
        return now >= due_time;
    }

    [[nodiscard]] bool is_cancelled() const noexcept {
        return metadata && metadata->cancellation_flag &&
               metadata->cancellation_flag->load(std::memory_order_relaxed);
    }

    /**
     * @brief Computes dynamic priority with starvation aging mitigation.
     * Every threshold ms waited beyond submission adds to effective priority rank.
     */
    [[nodiscard]] double effective_priority(
        std::chrono::steady_clock::time_point now,
        std::chrono::milliseconds aging_threshold) const noexcept {
        if (!metadata)
            return 0.0;
        double base = static_cast<double>(static_cast<uint8_t>(metadata->priority));
        if (aging_threshold.count() <= 0)
            return base;

        auto wait_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - metadata->submission_time);
        double age_boost = static_cast<double>(wait_duration.count()) /
                           static_cast<double>(aging_threshold.count());
        return base + age_boost;
    }
};

}  // namespace aegis
