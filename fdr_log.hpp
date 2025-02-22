/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#pragma once

#include "fdr_policy.hpp"

#include <fmt/core.h>

#include <source_location>

#ifdef FDR_USE_SPDLOG
#include <spdlog/spdlog.h>
#else
#include <phosphor-logging/lg2.hpp>
#endif /* FDR_USE_SPDLOG */

namespace fdrlog
{
#ifdef FDR_USE_SPDLOG
void InitLogger(const std::string LoggingLevel, const std::string LoggingPath,
                size_t LoggingFileMaxSize, size_t LoggingFileNumber);
#else
void InitLogger(const std::string LoggingLevel);
int LogLevel();
#endif

inline std::string getErrorInfoTexts(const std::string& errorLevel,
                                     const std::source_location& loc)
{
    //  std::string fileName =
    //  loc.file_name().substring(loc.file_name().find_last_of('/'));
    std::string res("[ " + errorLevel + " " + loc.file_name() + ":" +
                    std::to_string(loc.line()) + " ] ");
    return res;
};

template <typename... T>
inline void debug(std::string_view format, T&&... args)
{
    auto msg = fmt::format(fmt::runtime(format), std::forward<T>(args)...);
#ifdef FDR_USE_SPDLOG
    spdlog::debug(msg);
#else
    if (LogLevel() >= LOG_DEBUG)
        lg2::debug(msg.c_str());
#endif
};

template <typename... T>
inline void info(std::string_view format, T&&... args)
{
    auto msg = fmt::format(fmt::runtime(format), std::forward<T>(args)...);
#ifdef FDR_USE_SPDLOG
    spdlog::info(msg);
#else
    if (LogLevel() >= LOG_INFO)
        lg2::info(msg.c_str());
#endif
};

template <typename... Args>
struct warn
{
    warn(std::format_string<Args...> format, Args&&... args,
         [[maybe_unused]] const std::source_location& loc =
             std::source_location::current()) noexcept
    {
        std::string_view format_view = format.get();
        //    std::cout<< format_view<<'\n';
        auto msg = getErrorInfoTexts("WARN", loc);
        msg += fmt::format(fmt::runtime(format_view),
                           std::forward<Args>(args)...);
#ifdef FDR_USE_SPDLOG
        spdlog::warn(msg);
#else
        if (LogLevel() >= LOG_WARNING)
            lg2::warning(msg.c_str());
#endif
    };
};

template <typename... Args>
struct error
{
    error(std::format_string<Args...> format, Args&&... args,
          [[maybe_unused]] const std::source_location& loc =
              std::source_location::current()) noexcept
    {
        std::string_view format_view = format.get();
        auto msg = getErrorInfoTexts("ERROR", loc);
        msg += fmt::format(fmt::runtime(format_view),
                           std::forward<Args>(args)...);
#ifdef FDR_USE_SPDLOG
        spdlog::error(msg);
#else
        if (LogLevel() >= LOG_ERR)
            lg2::error(msg.c_str());
#endif
    }
};

// Deduction guide for error similar to warn.
template <typename... Args>
error(std::format_string<Args...>, Args&&...) -> error<Args...>;

template <typename... T>
inline void critical(std::string_view format, T&&... args)
{
    auto msg = fmt::format(fmt::runtime(format), std::forward<T>(args)...);
#ifdef FDR_USE_SPDLOG
    spdlog::critical(msg);
#else
    if (LogLevel() >= LOG_CRIT)
        lg2::critical(msg.c_str());
#endif
};

template <typename... Args>
warn(std::format_string<Args...>, Args&&...) -> warn<Args...>;
} // namespace fdrlog

namespace logThrottle
{
bool logThrottling(Info_t& info);
} // namespace logThrottle
