/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include <ctime>
#include <iostream>
#include <map>

#ifdef FDR_USE_SPDLOG
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#endif /* SPDLOG */

#ifndef LOG_ROTATION
#define LOG_ROTATION 86400 // 24 hours
#endif

#include "fdr_log.hpp"

#ifndef FDR_USE_SPDLOG
const std::map<std::string, int> logLevelMap = {
    {"debug", LOG_DEBUG},   // 7
    {"info", LOG_INFO},     // 6
    {"warn", LOG_WARNING},  // 4
    {"error", LOG_ERR},     // 3
    {"critical", LOG_CRIT}, // 2
};

static int logLevel = LOG_INFO;

#endif /* FDR_USE_SPDLOG */

namespace fdrlog
{
#ifdef FDR_USE_SPDLOG
void InitLogger(const std::string LoggingLevel, const std::string LoggingPath,
                size_t LoggingFileMaxSize, size_t LoggingFileNumber)
{
    spdlog::level::level_enum level = spdlog::level::from_str(LoggingLevel);

    std::vector<spdlog::sink_ptr> sinks;
    // stdout logger
    auto stdout_logger = std::make_shared<spdlog::sinks::stdout_sink_mt>();
    sinks.push_back(stdout_logger);
    // file logger
    try
    {
        auto file_logger =
            std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                LoggingPath, LoggingFileMaxSize, LoggingFileNumber);
        sinks.push_back(file_logger);
    }
    catch (const std::exception& e)
    {
        spdlog::error("Error creating logger file: {}", e.what());
    }

    // combined logger
    auto logger = std::make_shared<spdlog::logger>("fdr", begin(sinks),
                                                   end(sinks));
    logger->flush_on(level);
    logger->set_level(level);

    // set as default logger
    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);
}
#else
void InitLogger(const std::string LoggingLevel)
{
    if (auto enumLevel = logLevelMap.find(LoggingLevel);
        enumLevel != logLevelMap.end())
        logLevel = enumLevel->second;
    else
        std::cout << "LoggingLevel " << LoggingLevel
                  << "Not found. Using default info" << std::endl;
}

int LogLevel()
{
    return logLevel;
}
#endif
} // namespace fdrlog

namespace logThrottle
{
bool logThrottling(Info_t& info)
{
// Apply throttling only if disable from meta layer
#ifndef THROTTLE_LOG_DISABLE

    std::time_t current_time = std::time(nullptr);

    // If timestamp is 0, set it to the current time and return true
    if (info.timeStamp == 0)
    {
        info.timeStamp = static_cast<int>(current_time);
        return true;
    }

    int time_diff = static_cast<int>(current_time - info.timeStamp);

    // If the difference is >= repeatTime, update timestamp and return true
    if (time_diff >= LOG_ROTATION)
    {
        info.timeStamp = static_cast<int>(current_time);
        return true;
    }

    return false; // Throttle the log

#else
    return true; // Always log
#endif
}

} // namespace logThrottle
