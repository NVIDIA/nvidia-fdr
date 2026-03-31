/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for fdr_log.cpp — logging utilities
 *
 * Covers:
 *   - fdrlog::getErrorInfoTexts formatting
 *   - logThrottle::logThrottling: first call, subsequent, rotation reset
 */

#include "fdr_log.hpp"
#include "testCommon.hpp"

#include <source_location>

// --- getErrorInfoTexts() ---

TEST(FdrLog, GetErrorInfoTextsFormat)
{
    auto loc = std::source_location::current();
    auto result = fdrlog::getErrorInfoTexts("ERROR", loc);

    EXPECT_THAT(result, HasSubstr("ERROR"));
    EXPECT_THAT(result, HasSubstr(loc.file_name()));
    EXPECT_THAT(result, HasSubstr(std::to_string(loc.line())));
    EXPECT_THAT(result, HasSubstr("[ "));
    EXPECT_THAT(result, HasSubstr(" ] "));
}

TEST(FdrLog, GetErrorInfoTextsWarnLevel)
{
    auto loc = std::source_location::current();
    auto result = fdrlog::getErrorInfoTexts("WARN", loc);
    EXPECT_THAT(result, HasSubstr("WARN"));
}

// --- logThrottling() ---

TEST(FdrLog, LogThrottlingFirstCallReturnsTrue)
{
    Info_t info{};
    info.timeStamp = 0;
    EXPECT_TRUE(logThrottle::logThrottling(info));
    EXPECT_NE(info.timeStamp, 0);
}

TEST(FdrLog, LogThrottlingSubsequentCallReturnsFalse)
{
    Info_t info{};
    info.timeStamp = 0;

    EXPECT_TRUE(logThrottle::logThrottling(info));
    EXPECT_FALSE(logThrottle::logThrottling(info));
    EXPECT_FALSE(logThrottle::logThrottling(info));
}

TEST(FdrLog, LogThrottlingResetsAfterRotation)
{
    Info_t info{};
    time_t now = std::time(nullptr);
    info.timeStamp = static_cast<decltype(info.timeStamp)>(now - 86401);

    EXPECT_TRUE(logThrottle::logThrottling(info));
}

// --- InitLogger() / LogLevel() ---

TEST(FdrLog, InitLoggerSetsDebugLevel)
{
    fdrlog::InitLogger("debug");
    EXPECT_EQ(fdrlog::LogLevel(), LOG_DEBUG);
}

TEST(FdrLog, InitLoggerSetsWarnLevel)
{
    fdrlog::InitLogger("warn");
    EXPECT_EQ(fdrlog::LogLevel(), LOG_WARNING);
}

TEST(FdrLog, InitLoggerSetsErrorLevel)
{
    fdrlog::InitLogger("error");
    EXPECT_EQ(fdrlog::LogLevel(), LOG_ERR);
}

TEST(FdrLog, InitLoggerSetsCriticalLevel)
{
    fdrlog::InitLogger("critical");
    EXPECT_EQ(fdrlog::LogLevel(), LOG_CRIT);
}

TEST(FdrLog, InitLoggerSetsInfoLevel)
{
    fdrlog::InitLogger("info");
    EXPECT_EQ(fdrlog::LogLevel(), LOG_INFO);
}

TEST(FdrLog, InitLoggerInvalidLevelKeepsPrevious)
{
    fdrlog::InitLogger("debug");
    fdrlog::InitLogger("INVALID");
    EXPECT_EQ(fdrlog::LogLevel(), LOG_DEBUG);
    fdrlog::InitLogger("info");
}

// --- logThrottle edge cases ---

TEST(FdrLog, LogThrottlingExactRotationBoundary)
{
    Info_t info{};
    time_t now = std::time(nullptr);
    info.timeStamp = static_cast<decltype(info.timeStamp)>(now - 86400);
    EXPECT_TRUE(logThrottle::logThrottling(info));
}

TEST(FdrLog, LogThrottlingJustBeforeRotation)
{
    Info_t info{};
    time_t now = std::time(nullptr);
    info.timeStamp = static_cast<decltype(info.timeStamp)>(now - 86399);
    EXPECT_FALSE(logThrottle::logThrottling(info));
}

TEST(FdrLog, LogThrottlingMultipleResets)
{
    Info_t info{};
    info.timeStamp = 0;

    EXPECT_TRUE(logThrottle::logThrottling(info));
    EXPECT_FALSE(logThrottle::logThrottling(info));

    time_t now2 = std::time(nullptr);
    info.timeStamp = static_cast<decltype(info.timeStamp)>(now2 - 86401);
    EXPECT_TRUE(logThrottle::logThrottling(info));
    EXPECT_FALSE(logThrottle::logThrottling(info));
}

// --- getErrorInfoTexts with different levels ---

TEST(FdrLog, GetErrorInfoTextsDebugLevel)
{
    auto loc = std::source_location::current();
    auto result = fdrlog::getErrorInfoTexts("DEBUG", loc);
    EXPECT_THAT(result, HasSubstr("DEBUG"));
}

TEST(FdrLog, GetErrorInfoTextsInfoLevel)
{
    auto loc = std::source_location::current();
    auto result = fdrlog::getErrorInfoTexts("INFO", loc);
    EXPECT_THAT(result, HasSubstr("INFO"));
}

TEST(FdrLog, GetErrorInfoTextsCriticalLevel)
{
    auto loc = std::source_location::current();
    auto result = fdrlog::getErrorInfoTexts("CRITICAL", loc);
    EXPECT_THAT(result, HasSubstr("CRITICAL"));
}

// --- LogLevel reflects InitLogger ---

TEST(FdrLog, LogLevelReflectsInit)
{
    fdrlog::InitLogger("error");
    EXPECT_EQ(fdrlog::LogLevel(), LOG_ERR);
    fdrlog::InitLogger("critical");
    EXPECT_EQ(fdrlog::LogLevel(), LOG_CRIT);
    fdrlog::InitLogger("info");
    EXPECT_EQ(fdrlog::LogLevel(), LOG_INFO);
}
