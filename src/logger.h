#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <cstdarg>
#include <chrono>
#include <iomanip>
#include <source_location>
#include <sstream>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERR
};

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void init(const std::string& log_dir = "");
    
    // Printf-style logging functions

#ifdef NDEBUG
    // Release: no source location
    #if defined(__GNUC__) || defined(__clang__)
    void log(LogLevel level, const char* format, ...) __attribute__((format(printf, 3, 4)));
    #elif defined(_MSC_VER)
    void log(LogLevel level, const _Printf_format_string_ char* format, ...);
    #else
    void log(LogLevel level, const std::source_location& loc, const char* format, ...);
    #endif
#else
    // Debug: with source location
    #if defined(__GNUC__) || defined(__clang__)
    void log(LogLevel level, const std::source_location& loc, const char* format, ...) 
        __attribute__((format(printf, 4, 5)));
    #elif defined(_MSC_VER)
    void log(LogLevel level, const std::source_location& loc, const _Printf_format_string_ char* format, ...);
    #else
    void log(LogLevel level, const std::source_location& loc, const char* format, ...);
    #endif
#endif

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

// Macros handle the source_location injection
#ifdef NDEBUG
    #define LOG_DEBUG(...)   Logger::getInstance().log(LogLevel::DEBUG, __VA_ARGS__)
    #define LOG_INFO(...)    Logger::getInstance().log(LogLevel::INFO, __VA_ARGS__)
    #define LOG_WARNING(...) Logger::getInstance().log(LogLevel::WARNING, __VA_ARGS__)
    #define LOG_ERROR(...)   Logger::getInstance().log(LogLevel::ERR, __VA_ARGS__)
#else
    #define LOG_DEBUG(...)   Logger::getInstance().log(LogLevel::DEBUG, std::source_location::current(), __VA_ARGS__)
    #define LOG_INFO(...)    Logger::getInstance().log(LogLevel::INFO, std::source_location::current(), __VA_ARGS__)
    #define LOG_WARNING(...) Logger::getInstance().log(LogLevel::WARNING, std::source_location::current(), __VA_ARGS__)
    #define LOG_ERROR(...)   Logger::getInstance().log(LogLevel::ERR, std::source_location::current(), __VA_ARGS__)
#endif