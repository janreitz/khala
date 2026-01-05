#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <cstdarg>
#include <chrono>
#include <iomanip>
#include <sstream>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void init(const std::string& log_dir = "");
    
    // Printf-style logging functions
    void debug(const char* format, ...) __attribute__((format(printf, 2, 3)));
    void info(const char* format, ...) __attribute__((format(printf, 2, 3)));
    void warning(const char* format, ...) __attribute__((format(printf, 2, 3)));
    void error(const char* format, ...) __attribute__((format(printf, 2, 3)));

    // Non-copyable
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() = default;
    ~Logger();

    void log(LogLevel level, const char* format, va_list args);
    std::string formatMessage(LogLevel level, const std::string& message);
    std::string getCurrentTimestamp();
    std::string levelToString(LogLevel level);

    std::mutex mutex_;
    std::unique_ptr<std::ofstream> log_file_;
    bool initialized_ = false;
};

// Convenience macros for easy migration from printf
#define LOG_DEBUG(...) Logger::getInstance().debug(__VA_ARGS__)
#define LOG_INFO(...) Logger::getInstance().info(__VA_ARGS__)
#define LOG_WARNING(...) Logger::getInstance().warning(__VA_ARGS__)
#define LOG_ERROR(...) Logger::getInstance().error(__VA_ARGS__)