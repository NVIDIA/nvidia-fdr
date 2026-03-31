/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for fdr_common.cpp / fdr_common.hpp — utility functions
 *
 * Covers:
 *   - split(), FindAndReplaceAll()
 *   - convertStrToUint64(), CheckIsInRange()
 *   - strna(), getSubsRecordKey(), splitGroupFetchKeys()
 *   - GetDirectoryName()
 *   - LeakyBucket: init, add, full, partial, drain
 */

#include "testCommon.hpp"

#include "fdr_common.hpp"

#include <chrono>
#include <thread>

// --- split() ---

TEST(FdrCommon, SplitBasic)
{
    auto result = split("a,b,c", ',');
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "b");
    EXPECT_EQ(result[2], "c");
}

TEST(FdrCommon, SplitEmptyString)
{
    auto result = split("", ',');
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "");
}

TEST(FdrCommon, SplitTrailingDelimiter)
{
    auto result = split("a,b,", ',');
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[2], "");
}

TEST(FdrCommon, SplitSingleToken)
{
    auto result = split("hello", ',');
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "hello");
}

TEST(FdrCommon, SplitLeadingDelimiter)
{
    auto result = split(",a,b", ',');
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], "");
    EXPECT_EQ(result[1], "a");
}

TEST(FdrCommon, SplitConsecutiveDelimiters)
{
    auto result = split("a,,b", ',');
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[1], "");
}

// --- FindAndReplaceAll() ---

TEST(FdrCommon, FindAndReplaceAllNoMatch)
{
    std::string s = "hello world";
    FindAndReplaceAll(s, "xyz", "abc");
    EXPECT_EQ(s, "hello world");
}

TEST(FdrCommon, FindAndReplaceAllSingleMatch)
{
    std::string s = "hello world";
    FindAndReplaceAll(s, "world", "earth");
    EXPECT_EQ(s, "hello earth");
}

TEST(FdrCommon, FindAndReplaceAllMultipleMatches)
{
    std::string s = "aXbXcX";
    FindAndReplaceAll(s, "X", "YY");
    EXPECT_EQ(s, "aYYbYYcYY");
}

TEST(FdrCommon, FindAndReplaceAllEmptyReplace)
{
    std::string s = "a-b-c";
    FindAndReplaceAll(s, "-", "");
    EXPECT_EQ(s, "abc");
}

// --- convertStrToUint64() ---

TEST(FdrCommon, ConvertStrToUint64Valid)
{
    std::string err;
    EXPECT_EQ(convertStrToUint64("12345", err), 12345u);
}

TEST(FdrCommon, ConvertStrToUint64Zero)
{
    std::string err;
    EXPECT_EQ(convertStrToUint64("0", err), 0u);
}

TEST(FdrCommon, ConvertStrToUint64MaxValue)
{
    std::string err;
    EXPECT_EQ(convertStrToUint64("18446744073709551615", err), UINT64_MAX);
}

TEST(FdrCommon, ConvertStrToUint64InvalidString)
{
    std::string err;
    EXPECT_EQ(convertStrToUint64("not_a_number", err), 0u);
}

TEST(FdrCommon, ConvertStrToUint64Overflow)
{
    std::string err;
    EXPECT_EQ(convertStrToUint64("99999999999999999999999", err), 0u);
}

// --- CheckIsInRange() ---

TEST(FdrCommon, CheckIsInRangeInside)
{
    EXPECT_TRUE(CheckIsInRange(1, 10, 5));
}

TEST(FdrCommon, CheckIsInRangeBoundaries)
{
    EXPECT_TRUE(CheckIsInRange(1, 10, 1));
    EXPECT_TRUE(CheckIsInRange(1, 10, 10));
}

TEST(FdrCommon, CheckIsInRangeOutside)
{
    EXPECT_FALSE(CheckIsInRange(5, 10, 3));
    EXPECT_FALSE(CheckIsInRange(1, 10, 15));
}

TEST(FdrCommon, CheckIsInRangeSingleValue)
{
    EXPECT_TRUE(CheckIsInRange(5, 5, 5));
    EXPECT_FALSE(CheckIsInRange(5, 5, 6));
}

// --- strna() ---

TEST(FdrCommon, StrnaValidPointer)
{
    EXPECT_STREQ(strna("hello"), "hello");
}

TEST(FdrCommon, StrnaNullPointer)
{
    EXPECT_STREQ(strna(nullptr), "n/a");
}

TEST(FdrCommon, StrnaEmptyString)
{
    EXPECT_STREQ(strna(""), "");
}

// --- getSubsRecordKey() ---

TEST(FdrCommon, GetSubsRecordKeyBasic)
{
    std::string obj = "/xyz/openbmc_project/sensors/temperature/GPU0";
    std::string inf = "xyz.openbmc_project.Sensor.Value";
    std::string prop = "Value";
    EXPECT_EQ(getSubsRecordKey(obj, inf, prop),
              "/xyz/openbmc_project/sensors/temperature/GPU0/"
              "xyz.openbmc_project.Sensor.Value/Value");
}

TEST(FdrCommon, GetSubsRecordKeyEmpty)
{
    std::string obj, inf, prop;
    EXPECT_EQ(getSubsRecordKey(obj, inf, prop), "//");
}

// --- splitGroupFetchKeys() ---

TEST(FdrCommon, SplitGroupFetchKeysBasic)
{
    auto result = splitGroupFetchKeys("Shmem/ns1");
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], "Shmem");
    EXPECT_EQ(result[1], "ns1");
}

TEST(FdrCommon, SplitGroupFetchKeysSingleSegment)
{
    auto result = splitGroupFetchKeys("Command");
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "Command");
}

TEST(FdrCommon, SplitGroupFetchKeysMultipleSlashes)
{
    auto result = splitGroupFetchKeys("a/b/c");
    ASSERT_EQ(result.size(), 3u);
}

// --- GetDirectoryName() ---

TEST(FdrCommon, GetDirectoryName)
{
    bootCounter = "42";
    sensorDirTimestamp = "20240315_120000";
    EXPECT_EQ(GetDirectoryName(), "BootCount_42_DateStamp_20240315_120000");
}

// --- LeakyBucket ---

TEST(FdrCommon, LeakyBucketInit)
{
    LeakyBucket bucket(10, 1.0);
    EXPECT_EQ(bucket.Capacity(), 10);
    EXPECT_FLOAT_EQ(bucket.Rate(), 1.0f);
    EXPECT_EQ(bucket.Count(), 0);
    EXPECT_FALSE(bucket.IsFull());
}

TEST(FdrCommon, LeakyBucketAddWithinCapacity)
{
    LeakyBucket bucket(10, 1.0);
    EXPECT_EQ(bucket.Add(5), 5);
    EXPECT_GT(bucket.Count(), 0);
    EXPECT_FALSE(bucket.IsFull());
}

TEST(FdrCommon, LeakyBucketAddToCapacity)
{
    LeakyBucket bucket(5, 0.001);
    EXPECT_EQ(bucket.Add(5), 5);
    EXPECT_TRUE(bucket.IsFull());
}

TEST(FdrCommon, LeakyBucketAddWhenFull)
{
    LeakyBucket bucket(5, 0.001);
    bucket.Add(5);
    EXPECT_EQ(bucket.Add(1), 0);
}

TEST(FdrCommon, LeakyBucketPartialAdd)
{
    LeakyBucket bucket(5, 0.001);
    bucket.Add(3);
    EXPECT_EQ(bucket.Add(10), 2);
}

TEST(FdrCommon, LeakyBucketDrains)
{
    LeakyBucket bucket(5, 100.0);
    bucket.Add(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(bucket.Count(), 0);
    EXPECT_FALSE(bucket.IsFull());
}
