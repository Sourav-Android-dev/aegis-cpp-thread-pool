#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

namespace aegis {

enum class LogLevel : uint8_t { Debug = 0, Info = 1, Warn = 2, Error = 3, Off = 4 };

constexpr std::string_view to_string(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Off:
            return "OFF";
    }
    return "UNKNOWN";
}

/**
 * @brief Thread-safe lightweight structured logger.
 */
class Logger {
public:
    using Sink = std::function<void(LogLevel, std::string_view)>;

    static Logger& instance();

    void set_level(LogLevel level) noexcept;
    LogLevel level() const noexcept;

    void set_sink(Sink sink);
    void reset_sink();

    void log(LogLevel level, std::string_view message);

    template <typename... Args>
    void log_formatted(LogLevel level, Args&&... args) {
        if (level < level_.load(std::memory_order_relaxed)) {
            return;
        }
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        log(level, oss.str());
    }

private:
    Logger();
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::atomic<LogLevel> level_{LogLevel::Info};
    mutable std::mutex sink_mutex_;
    Sink sink_;
};

#define AEGIS_LOG_DEBUG(...) \
    aegis::Logger::instance().log_formatted(aegis::LogLevel::Debug, __VA_ARGS__)
#define AEGIS_LOG_INFO(...) \
    aegis::Logger::instance().log_formatted(aegis::LogLevel::Info, __VA_ARGS__)
#define AEGIS_LOG_WARN(...) \
    aegis::Logger::instance().log_formatted(aegis::LogLevel::Warn, __VA_ARGS__)
#define AEGIS_LOG_ERROR(...) \
    aegis::Logger::instance().log_formatted(aegis::LogLevel::Error, __VA_ARGS__)

}  // namespace aegis
