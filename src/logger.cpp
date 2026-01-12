#include "logger.h"
#include "utility.h"

#include <filesystem>
#include <iostream>
#include <cstdlib>

namespace fs = std::filesystem;

void Logger::init(const std::string& log_dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return;
    }

    fs::path dir(log_dir);
    if (dir.empty() || !fs::exists(dir)) {
        // Use KHALA_DATA_DIR or fallback to XDG_DATA_HOME or ~/.local/share
        const char* khala_data = std::getenv("KHALA_DATA_DIR");
        if (khala_data) {
            dir = std::string(khala_data) + "/logs";
        } else {
            const char* xdg_data = std::getenv("XDG_DATA_HOME");
            if (xdg_data) {
                dir = std::string(xdg_data) + "/khala/logs";
            } else {
                const auto home = platform::get_home_dir();
                if (home) {
                    dir = home.value() / ".local/share/khala/logs";
                } else {
                    dir = platform::get_temp_dir() / "khala/logs";
                }
            }
        }
    }

    try {
        std::filesystem::create_directories(dir);
        
        auto now = std::chrono::system_clock::now();
        auto time_value = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << dir << "/khala_" << std::put_time(std::localtime(&time_value), "%Y%m%d_%H%M%S") << ".log";
        
        log_file_ = std::make_unique<std::ofstream>(ss.str(), std::ios::app);
        if (!log_file_->is_open()) {
            std::cerr << "Warning: Failed to open log file " << ss.str() << std::endl;
            log_file_.reset();
        } else {
            initialized_ = true;
            // Log initialization message
            *log_file_ << formatMessage(LogLevel::INFO, "Logger initialized") << std::endl;
            log_file_->flush();
        }
    } catch (const std::exception& e) {
        std::cerr << "Warning: Failed to initialize logger: " << e.what() << std::endl;
    }
}

Logger::~Logger() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (log_file_ && log_file_->is_open()) {
        *log_file_ << formatMessage(LogLevel::INFO, "Logger shutting down") << std::endl;
        log_file_->close();
    }
}

void Logger::debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log(LogLevel::DEBUG, format, args);
    va_end(args);
}

void Logger::info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log(LogLevel::INFO, format, args);
    va_end(args);
}

void Logger::warning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log(LogLevel::WARNING, format, args);
    va_end(args);
}

void Logger::error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log(LogLevel::ERR, format, args);
    va_end(args);
}

void Logger::log(LogLevel level, const char* format, va_list args) {
    // Format the message
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), format, args);

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

std::string Logger::formatMessage(LogLevel level, const std::string& message) {
    std::stringstream ss;
    ss << "[" << getCurrentTimestamp() << "] "
       << "[" << levelToString(level) << "] "
       << message;
    return ss.str();
}

std::string Logger::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_value = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_value), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARN";
        case LogLevel::ERR:   return "ERROR";
        default:                return "UNKNOWN";
    }
}