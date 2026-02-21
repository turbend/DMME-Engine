#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <string>
#include <mutex>
#include <vector>
#include <filesystem>

namespace dmme {
namespace utils {

class Logger {
public:
    static bool Initialize(const std::string& appName = "DMME",
                           const std::string& logDir  = "logs") {
        std::lock_guard<std::mutex> lock(s_initMutex);
        if (s_initialized) {
            return true;
        }

        try {
            std::filesystem::create_directories(logDir);

            std::string logPath = logDir + "/" + appName + ".log";

            auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            consoleSink->set_level(spdlog::level::debug);
            consoleSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");

            auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath, true);
            fileSink->set_level(spdlog::level::trace);
            fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");

            std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};
            s_logger = std::make_shared<spdlog::logger>(appName, sinks.begin(), sinks.end());
            s_logger->set_level(spdlog::level::trace);
            s_logger->flush_on(spdlog::level::warn);

            spdlog::set_default_logger(s_logger);
            spdlog::flush_every(std::chrono::seconds(3));

            s_initialized = true;
            s_logger->info("Logger initialized: {}", logPath);
            return true;

        } catch (const spdlog::spdlog_ex& ex) {
            fprintf(stderr, "Logger init failed: %s\n", ex.what());
            return false;
        }
    }

    static void Shutdown() {
        std::lock_guard<std::mutex> lock(s_initMutex);
        if (s_initialized) {
            s_logger->info("Logger shutting down");
            s_logger->flush();
            spdlog::shutdown();
            s_logger.reset();
            s_initialized = false;
        }
    }

    static std::shared_ptr<spdlog::logger>& Get() {
        return s_logger;
    }

private:
    static inline std::shared_ptr<spdlog::logger> s_logger = nullptr;
    static inline bool s_initialized = false;
    static inline std::mutex s_initMutex;
};

} // namespace utils
} // namespace dmme

// Production-grade logging macros with source location
#define DMME_LOG_TRACE(...)    if(::dmme::utils::Logger::Get()) SPDLOG_LOGGER_TRACE(::dmme::utils::Logger::Get(), __VA_ARGS__)
#define DMME_LOG_DEBUG(...)    if(::dmme::utils::Logger::Get()) SPDLOG_LOGGER_DEBUG(::dmme::utils::Logger::Get(), __VA_ARGS__)
#define DMME_LOG_INFO(...)     if(::dmme::utils::Logger::Get()) SPDLOG_LOGGER_INFO(::dmme::utils::Logger::Get(), __VA_ARGS__)
#define DMME_LOG_WARN(...)     if(::dmme::utils::Logger::Get()) SPDLOG_LOGGER_WARN(::dmme::utils::Logger::Get(), __VA_ARGS__)
#define DMME_LOG_ERROR(...)    if(::dmme::utils::Logger::Get()) SPDLOG_LOGGER_ERROR(::dmme::utils::Logger::Get(), __VA_ARGS__)
#define DMME_LOG_CRITICAL(...) if(::dmme::utils::Logger::Get()) SPDLOG_LOGGER_CRITICAL(::dmme::utils::Logger::Get(), __VA_ARGS__)