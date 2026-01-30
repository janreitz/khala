#include "logger.h"
#include "utility.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>

namespace fs = std::filesystem;

void Logger::init(const fs::path &log_dir)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        return;
    }

    try {
        std::filesystem::create_directories(log_dir);

        auto now = std::chrono::system_clock::now();
        auto time_value = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << platform::path_to_string(log_dir) << "/khala_"
           << std::put_time(std::localtime(&time_value), "%Y%m%d_%H%M%S")
           << ".log";

        log_file_ = std::make_unique<std::ofstream>(ss.str(), std::ios::app);
        if (!log_file_->is_open()) {
            std::cerr << "Warning: Failed to open log file " << ss.str()
                      << std::endl;
            log_file_.reset();
        } else {
            initialized_ = true;
            // Log initialization message
            *log_file_ << formatMessage(LogLevel::INFO, "Logger initialized")
                       << std::endl;
            log_file_->flush();
        }
    } catch (const std::exception &e) {
        std::cerr << "Warning: Failed to initialize logger: " << e.what()
                  << std::endl;
    }
}

Logger::~Logger()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (log_file_ && log_file_->is_open()) {
        *log_file_ << formatMessage(LogLevel::INFO, "Logger shutting down")
                   << std::endl;
        log_file_->close();
    }
}

#ifdef NDEBUG
void Logger::log(LogLevel level, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    // Format the message
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), format, args);

    va_end(args);

    std::string formatted_msg = formatMessage(level, std::string(buffer));

    std::lock_guard<std::mutex> lock(mutex_);

    // Always output to stdout
    fprintf(stdout, "%s\n", formatted_msg.c_str());
    fflush(stdout);

    // Write to file if available
    if (log_file_ && log_file_->is_open()) {
        *log_file_ << formatted_msg << std::endl;
        log_file_->flush();
    }
}
#else
void Logger::log(LogLevel level, const std::source_location &loc,
                 const char *format, ...)
{
    va_list args;
    va_start(args, format);

    // Format the message
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), format, args);

    va_end(args);

    // Extract just the filename from the full path
    std::string_view file_path = loc.file_name();
    auto last_slash = file_path.find_last_of("/\\");
    std::string_view filename = (last_slash != std::string_view::npos)
                                    ? file_path.substr(last_slash + 1)
                                    : file_path;

    // Build the formatted message with source location
    std::ostringstream oss;
    oss << getCurrentTimestamp() << " [" << levelToString(level) << "] " << "["
        << filename << ":" << loc.line() << " " << loc.function_name() << "] "
        << buffer;

    std::string formatted_msg = oss.str();

    std::lock_guard<std::mutex> lock(mutex_);

    // Always output to stdout
    fprintf(stdout, "%s\n", formatted_msg.c_str());
    fflush(stdout);

    // Write to file if available
    if (log_file_ && log_file_->is_open()) {
        *log_file_ << formatted_msg << std::endl;
        log_file_->flush();
    }
}
#endif

std::string Logger::formatMessage(LogLevel level, const std::string &message)
{
    std::stringstream ss;
    ss << "[" << getCurrentTimestamp() << "] " << "[" << levelToString(level)
       << "] " << message;
    return ss.str();
}

std::string Logger::getCurrentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time_value = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_value), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string Logger::levelToString(LogLevel level)
{
    switch (level) {
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::WARNING:
        return "WARN";
    case LogLevel::ERR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}