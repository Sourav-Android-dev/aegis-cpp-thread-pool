#include "aegis/logger.hpp"

#include <iomanip>
#include <sstream>
#include <thread>

namespace aegis {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

Logger::Logger() {
    reset_sink();
}

void Logger::set_level(LogLevel level) noexcept {
    level_.store(level, std::memory_order_relaxed);
}

LogLevel Logger::level() const noexcept {
    return level_.load(std::memory_order_relaxed);
}

void Logger::set_sink(Sink sink) {
    std::lock_guard<std::mutex> lock(sink_mutex_);
    sink_ = std::move(sink);
}

void Logger::reset_sink() {
    std::lock_guard<std::mutex> lock(sink_mutex_);
    sink_ = [](LogLevel lvl, std::string_view msg) {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::tm tm_buf{};
#if defined(_WIN32)
        localtime_s(&tm_buf, &in_time_t);
#else
        localtime_r(&in_time_t, &tm_buf);
#endif

        std::ostringstream ss;
        ss << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0')
           << std::setw(3) << ms.count() << "] "
           << "[" << std::setw(5) << to_string(lvl) << "] "
           << "[tid:" << std::this_thread::get_id() << "] " << msg << "\n";

        std::cout << ss.str() << std::flush;
    };
}

void Logger::log(LogLevel level, std::string_view message) {
    if (level < level_.load(std::memory_order_relaxed)) {
        return;
    }

    std::lock_guard<std::mutex> lock(sink_mutex_);
    if (sink_) {
        sink_(level, message);
    }
}

}  // namespace aegis
