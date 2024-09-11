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

#include <fmt/core.h>

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

template <typename... T>
inline void warn(std::string_view format, T&&... args)
{
    auto msg = fmt::format(fmt::runtime(format), std::forward<T>(args)...);
#ifdef FDR_USE_SPDLOG
    spdlog::warn(msg);
#else
    if (LogLevel() >= LOG_WARNING)
        lg2::warning(msg.c_str());
#endif
};

template <typename... T>
inline void error(std::string_view format, T&&... args)
{
    auto msg = fmt::format(fmt::runtime(format), std::forward<T>(args)...);
#ifdef FDR_USE_SPDLOG
    spdlog::info(msg);
#else
    if (LogLevel() >= LOG_ERR)
        lg2::error(msg.c_str());
#endif
};

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
} // namespace fdrlog
