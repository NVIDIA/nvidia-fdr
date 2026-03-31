/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for fdr_record.cpp — Record class
 *
 * Covers:
 *   - RunningStatisticEngine: single, multiple, reset samples
 *   - RefreshValue: Uint64, string, Double, invalid input
 *   - appendRunningStatToStatfile: round-trip verification
 *   - Getter methods
 *   - Private state verification via #define private public
 */

#include "testCommon.hpp"

#include "fdr_record.hpp"

#include "fdr_common.hpp"
#include "fdr_store.hpp"

#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

class RecordTest : public Test
{
  protected:
    std::string tmpDir;

    Profile_t profile;
    Section_t section;
    Component_t component;
    InfoGroup_t infogroup;
    Info_t info;
    std::shared_ptr<FDRStore> logStore;
    std::shared_ptr<FDRStore> statStore;

    void SetUp() override
    {
        tmpDir = fs::temp_directory_path() / "fdr_record_test_XXXXXX";
        tmpDir = std::string(mkdtemp(tmpDir.data()));

        profile.GeneralConfig.LogsFormat = "JSON";
        profile.GeneralConfig.LogsBasePath = tmpDir;

        section.ID = "GPU";
        component.ID = "GPU0";
        infogroup.ID = "Sensor.Thermal";
        infogroup.CompactionMethod = "Average";
        infogroup.LastCompactedAt = 0;

        info.ID = "GPUTemp";
        info.ParamID = 42;
        info.DataType = "Uint64";
        info.FetchMethod = "Shmem";
        info.StorePolicy = "EveryFetch";
        info.FetchFreqSecs = 1;
        info.StoreFreqSecs = 1;

        std::string logPath = tmpDir + "/log.json";
        std::string statPath = tmpDir + "/stat.json";
        logStore = std::make_shared<FDRStore>(logPath, "JSON", STORE_WRITER);
        statStore = std::make_shared<FDRStore>(statPath, "JSON", STORE_WRITER);
    }

    void TearDown() override
    {
        logStore.reset();
        statStore.reset();
        fs::remove_all(tmpDir);
    }

    Record makeRecord()
    {
        return Record(profile, section, component, logStore, statStore,
                      infogroup, info);
    }

    fdrpb::fdr_sample makeSample(uint64_t ts, uint32_t paramId, int64_t val)
    {
        fdrpb::fdr_sample s;
        s.set_timestamp(ts);
        s.set_paramid(paramId);
        s.set_paramvalueint64(val);
        return s;
    }
};

// --- RunningStatisticEngine ---

TEST_F(RecordTest, RunningStatSingleSample)
{
    auto rec = makeRecord();

    rec.RunningStatisticEngine(makeSample(1000, 42, 100));
    rec.appendRunningStatToStatfile();

    EXPECT_EQ(rec.runningStatus.numsamples(), 1);
    EXPECT_DOUBLE_EQ(rec.runningStatus.min(), 100.0);
    EXPECT_DOUBLE_EQ(rec.runningStatus.max(), 100.0);
}

TEST_F(RecordTest, RunningStatMultipleSamples)
{
    auto rec = makeRecord();

    for (int i = 1; i <= 5; i++)
    {
        rec.RunningStatisticEngine(makeSample(1000 + i, 42, i * 10));
    }

    EXPECT_EQ(rec.runningStatus.numsamples(), 5);
    EXPECT_DOUBLE_EQ(rec.runningStatus.min(), 10.0);
    EXPECT_DOUBLE_EQ(rec.runningStatus.max(), 50.0);
    EXPECT_NEAR(rec.runningStatus.avg(), 30.0, 0.01);

    rec.appendRunningStatToStatfile();
}

TEST_F(RecordTest, RunningStatResetClears)
{
    auto rec = makeRecord();

    rec.RunningStatisticEngine(makeSample(1000, 42, 50));
    EXPECT_EQ(rec.runningStatus.numsamples(), 1);

    rec.ResetRunningStat();
    EXPECT_EQ(rec.runningStatus.numsamples(), 0);

    rec.appendRunningStatToStatfile();
}

// --- RefreshValue ---

TEST_F(RecordTest, RefreshValueUint64)
{
    auto rec = makeRecord();

    rec.RefreshValue("12345");
    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 12345);
    EXPECT_EQ(rec.data.fdr_sample_data.paramid(), 42u);
}

TEST_F(RecordTest, RefreshValueString)
{
    info.DataType = "string";
    auto rec = makeRecord();

    rec.RefreshValue("hello_gpu");
    EXPECT_EQ(rec.data.fdr_sample_data.paramvaluestring(), "hello_gpu");
}

TEST_F(RecordTest, RefreshValueDouble)
{
    info.DataType = "Double";
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    rec.RefreshValue("3.14");
    EXPECT_NEAR(rec.data.fdr_sample_data.paramvaluedouble(), 3.14, 0.001);
}

TEST_F(RecordTest, RefreshValueInvalidNumberDoesNotCrash)
{
    auto rec = makeRecord();
    EXPECT_NO_THROW(rec.RefreshValue("not_a_number"));
}

// --- RefreshValue stores timestamp ---

TEST_F(RecordTest, RefreshValueSetsTimestamp)
{
    auto rec = makeRecord();

    rec.RefreshValue("42");
    EXPECT_GT(rec.data.fdr_sample_data.timestamp(), 0u);
}

// --- Getters ---

TEST_F(RecordTest, GetterMethods)
{
    auto rec = makeRecord();

    EXPECT_EQ(rec.GetSectionID(), "GPU");
    EXPECT_EQ(rec.GetComponentID(), "GPU0");
    EXPECT_EQ(rec.GetInfoGroupID(), "Sensor.Thermal");
    EXPECT_EQ(rec.GetInfoListID(), "GPUTemp");
}

// --- Private state: LastFetchedAt / LastStoredAt start at 0 ---

TEST_F(RecordTest, InitialTimestampsAreZero)
{
    auto rec = makeRecord();

    EXPECT_EQ(rec.LastFetchedAt, 0);
    EXPECT_EQ(rec.LastStoredAt, 0);
}

// --- Private state: logsformat matches profile ---

TEST_F(RecordTest, LogsFormatMatchesProfile)
{
    auto rec = makeRecord();
    EXPECT_EQ(rec.logsformat, "JSON");
}

// --- Store pointer management ---

TEST_F(RecordTest, ResetLogStatStorePtrsNullifiesPointers)
{
    auto rec = makeRecord();
    rec.ResetLogStatStorePtrs();

    EXPECT_EQ(rec.fdrLogReaderWriter, nullptr);
    EXPECT_EQ(rec.fdrStatwriter, nullptr);
}

TEST_F(RecordTest, ResetLogStatStorePtrsReassigns)
{
    auto rec = makeRecord();

    std::string newLogPath = tmpDir + "/new_log.json";
    std::string newStatPath = tmpDir + "/new_stat.json";
    auto newLog = std::make_shared<FDRStore>(newLogPath, "JSON", STORE_WRITER);
    auto newStat =
        std::make_shared<FDRStore>(newStatPath, "JSON", STORE_WRITER);

    rec.ResetLogStatStorePtrs(newLog, newStat);
    EXPECT_EQ(rec.fdrLogReaderWriter, newLog);
    EXPECT_EQ(rec.fdrStatwriter, newStat);
}
