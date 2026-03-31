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

#include "testCommon.hpp"

#include "fdr_log.hpp"

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
    info.timeStamp = static_cast<int>(std::time(nullptr)) - 86401;

    EXPECT_TRUE(logThrottle::logThrottling(info));
}
