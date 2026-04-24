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

#include "fdr_common.hpp"
#include "testCommon.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
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
    struct Guard
    {
        std::string savedBoot = bootCounter;
        std::string savedTs = sensorDirTimestamp;
        ~Guard()
        {
            bootCounter = savedBoot;
            sensorDirTimestamp = savedTs;
        }
    } guard;

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

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(500);
    while (bucket.Count() > 0 && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(bucket.Count(), 0);
    EXPECT_FALSE(bucket.IsFull());
}

// --- exec() ---

TEST(FdrCommon, ExecEchoCommand)
{
    auto result = exec("echo hello");
    EXPECT_EQ(result.cmdExitstatus, 0);
    EXPECT_EQ(result.cmdOutput, "hello\n");
}

TEST(FdrCommon, ExecFailedCommand)
{
    auto result = exec("false");
    EXPECT_NE(result.cmdExitstatus, 0);
}

TEST(FdrCommon, ExecMultiLineOutput)
{
    auto result = exec("printf 'line1\\nline2\\n'");
    EXPECT_EQ(result.cmdExitstatus, 0);
    EXPECT_EQ(result.cmdOutput, "line1\nline2\n");
}

// --- copyFile() ---

TEST(FdrCommon, CopyFileSuccess)
{
    namespace fs = std::filesystem;
    auto tmpDir =
        fs::temp_directory_path() /
        ("fdr_copy_test_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(tmpDir);

    auto src = (tmpDir / "source.txt").string();
    auto dst = (tmpDir / "dest.txt").string();
    std::ofstream(src) << "test content";

    EXPECT_EQ(copyFile(src, dst), FDR_SUCCESS);

    std::ifstream in(dst);
    std::string content;
    std::getline(in, content);
    EXPECT_EQ(content, "test content");

    fs::remove_all(tmpDir);
}

TEST(FdrCommon, CopyFileSourceNotFound)
{
    EXPECT_EQ(copyFile("/nonexistent/file.txt", "/tmp/dst.txt"),
              FDR_ERR_FILE_COPY_FAIL);
}

// --- BkupAndDeleteCorruptFile() ---

TEST(FdrCommon, BkupAndDeleteCreatesBackupAndDeletes)
{
    namespace fs = std::filesystem;
    auto tmpDir =
        fs::temp_directory_path() /
        ("fdr_bkup_test_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(tmpDir);

    auto corrupt = (tmpDir / "corrupt.bin").string();
    std::ofstream(corrupt) << "corrupt data";

    EXPECT_TRUE(fs::exists(corrupt));
    BkupAndDeleteCorruptFile(corrupt);
    EXPECT_FALSE(fs::exists(corrupt));

    bool foundBackup = false;
    for (auto& entry : fs::directory_iterator(tmpDir))
    {
        if (entry.path().string().find("-corrupt-") != std::string::npos)
        {
            foundBackup = true;
        }
    }
    EXPECT_TRUE(foundBackup);

    fs::remove_all(tmpDir);
}

// --- FindAndReplaceFist() (typo in production code; header declares
// FindAndReplaceFirst but .cpp defines FindAndReplaceFist) ---

extern void FindAndReplaceFist(std::string& s, const std::string& search,
                               const std::string& replace);

TEST(FdrCommon, FindAndReplaceFistFound)
{
    std::string s = "hello world world";
    FindAndReplaceFist(s, "world", "earth");
    EXPECT_EQ(s, "hello earth world");
}

TEST(FdrCommon, FindAndReplaceFistNotFound)
{
    std::string s = "hello world";
    FindAndReplaceFist(s, "xyz", "abc");
    EXPECT_EQ(s, "hello world");
}

// --- get_procuptime() ---

TEST(FdrCommon, GetProcuptimeReturnsNonEmpty)
{
    auto result = get_procuptime();
    EXPECT_FALSE(result.empty());
    EXPECT_NE(result.find('.'), std::string::npos);
}

// --- fdrutil::warnFdrLowSpace() ---

TEST(FdrCommon, WarnFdrLowSpaceValidPath)
{
    EXPECT_NO_THROW(fdrutil::warnFdrLowSpace("/tmp"));
}

TEST(FdrCommon, WarnFdrLowSpaceInvalidPath)
{
    EXPECT_NO_THROW(fdrutil::warnFdrLowSpace("/nonexistent/path/xyz"));
}

// --- getGroupPollRecordKey() ---

TEST(FdrCommon, GetGroupPollRecordKeyShmem)
{
    Info_t testInfo{};
    testInfo.FetchMethod = "Shmem";
    testInfo.ShmemParams.Namespace = "ns1";
    EXPECT_EQ(getGroupPollRecordKey(testInfo), "Shmem/ns1");
}

TEST(FdrCommon, GetGroupPollRecordKeyShmemEmptyNamespace)
{
    Info_t testInfo{};
    testInfo.FetchMethod = "Shmem";
    testInfo.ShmemParams.Namespace = "";
    EXPECT_EQ(getGroupPollRecordKey(testInfo), "");
}

TEST(FdrCommon, GetGroupPollRecordKeyNonShmem)
{
    Info_t testInfo{};
    testInfo.FetchMethod = "DBUS";
    EXPECT_EQ(getGroupPollRecordKey(testInfo), "");
}

// --- systemUtils::checkEnvValue() ---

TEST(FdrCommon, CheckEnvValueCommandNotFound)
{
    EXPECT_FALSE(systemUtils::checkEnvValue("NONEXISTENT_VAR", "value"));
}

// --- FindAndReplaceAll edge cases ---

TEST(FdrCommon, FindAndReplaceAllOverlapping)
{
    std::string s = "aaaa";
    FindAndReplaceAll(s, "aa", "b");
    EXPECT_EQ(s, "bb");
}

TEST(FdrCommon, FindAndReplaceAllEntireString)
{
    std::string s = "hello";
    FindAndReplaceAll(s, "hello", "world");
    EXPECT_EQ(s, "world");
}

// --- convertStrToUint64 edge cases ---

TEST(FdrCommon, ConvertStrToUint64EmptyString)
{
    std::string err;
    EXPECT_EQ(convertStrToUint64("", err), 0u);
}

TEST(FdrCommon, ConvertStrToUint64LeadingSpaces)
{
    std::string err;
    EXPECT_EQ(convertStrToUint64("  123", err), 123u);
}

// --- LeakyBucket edge cases ---

TEST(FdrCommon, LeakyBucketAddZero)
{
    LeakyBucket bucket(10, 1.0);
    EXPECT_EQ(bucket.Add(0), 0);
    EXPECT_EQ(bucket.Count(), 0);
}

TEST(FdrCommon, LeakyBucketHighRate)
{
    LeakyBucket bucket(100, 10000.0);
    bucket.Add(1);

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(500);
    while (bucket.Count() > 0 && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(bucket.Count(), 0);
}

// --- BkupAndDeleteCorruptFile with nonexistent file ---

TEST(FdrCommon, BkupAndDeleteNonexistentFile)
{
    EXPECT_NO_THROW(BkupAndDeleteCorruptFile("/tmp/nonexistent_file_xyz.bin"));
}

// --- exec edge cases ---

TEST(FdrCommon, ExecEmptyOutput)
{
    auto result = exec("true");
    EXPECT_EQ(result.cmdExitstatus, 0);
    EXPECT_TRUE(result.cmdOutput.empty());
}

// --- copyFile overwrite existing ---

TEST(FdrCommon, CopyFileOverwriteExisting)
{
    namespace fs = std::filesystem;
    auto tmpDir =
        fs::temp_directory_path() /
        ("fdr_copy_overwrite_test_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(tmpDir);

    auto src = (tmpDir / "source.txt").string();
    auto dst = (tmpDir / "dest.txt").string();
    std::ofstream(src) << "new content";
    std::ofstream(dst) << "old content";

    EXPECT_EQ(copyFile(src, dst), FDR_SUCCESS);

    std::ifstream in(dst);
    std::string content;
    std::getline(in, content);
    EXPECT_EQ(content, "new content");

    fs::remove_all(tmpDir);
}

// --- split edge cases ---

TEST(FdrCommon, SplitAllDelimiters)
{
    auto result = split(",,,", ',');
    ASSERT_EQ(result.size(), 4u);
    for (auto& s : result)
    {
        EXPECT_TRUE(s.empty());
    }
}

// --- GetDirectoryName with empty globals ---

TEST(FdrCommon, GetDirectoryNameEmptyGlobals)
{
    struct Guard
    {
        std::string savedBoot = bootCounter;
        std::string savedTs = sensorDirTimestamp;
        ~Guard()
        {
            bootCounter = savedBoot;
            sensorDirTimestamp = savedTs;
        }
    } guard;

    bootCounter = "";
    sensorDirTimestamp = "";
    EXPECT_EQ(GetDirectoryName(), "BootCount__DateStamp_");
}

// --- CheckIsInRange with zeros ---

TEST(FdrCommon, CheckIsInRangeZeroRange)
{
    EXPECT_TRUE(CheckIsInRange(0, 0, 0));
    EXPECT_FALSE(CheckIsInRange(0, 0, 1));
}

// --- getSubsRecordKey with special characters ---

TEST(FdrCommon, GetSubsRecordKeySpecialChars)
{
    std::string obj = "/a/b";
    std::string inf = "c.d";
    std::string prop = "e";
    EXPECT_EQ(getSubsRecordKey(obj, inf, prop), "/a/b/c.d/e");
}
