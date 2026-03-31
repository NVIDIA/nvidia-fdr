/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for fdr_record.cpp — Record class
 *
 * Covers:
 *   - RunningStatisticEngine: single, multiple, reset samples
 *   - RefreshValue: Uint64, string, Double, invalid input
 *   - appendRunningStatToStatfile: in-memory + persist/readback verification
 *   - Getter methods
 *   - Private state verification via #define private public
 */

// clang-format off
#include "testCommon.hpp"
#include "fdr_common.hpp"
#include "fdr_record.hpp"
#include "fdr_store.hpp"
#include "property_variant.hpp"
// clang-format on

#include <chrono>
#include <filesystem>
#include <memory>

extern PropertyVariant g_stubDbusReturnValue;
extern RetCoreApi g_stubDbusDGDReturnValue;
extern PassthroughFPGA g_stubDbusPTReturnValue;

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
        auto uniquePath =
            fs::temp_directory_path() /
            ("fdr_record_test_" +
             std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(uniquePath);
        tmpDir = uniquePath.string();

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
    rec.data.paramtype = "Uint64";

    rec.RunningStatisticEngine(makeSample(1000, 42, 100));

    EXPECT_EQ(rec.runningStatus.numsamples(), 1);
    EXPECT_DOUBLE_EQ(rec.runningStatus.min(), 100.0);
    EXPECT_DOUBLE_EQ(rec.runningStatus.max(), 100.0);

    rec.appendRunningStatToStatfile();
    EXPECT_DOUBLE_EQ(rec.runningStatus.avg(), 100.0);
}

TEST_F(RecordTest, RunningStatMultipleSamples)
{
    auto rec = makeRecord();
    rec.data.paramtype = "Uint64";

    for (int i = 1; i <= 5; i++)
    {
        rec.RunningStatisticEngine(makeSample(1000 + i, 42, i * 10));
    }

    EXPECT_EQ(rec.runningStatus.numsamples(), 5);
    EXPECT_DOUBLE_EQ(rec.runningStatus.min(), 10.0);
    EXPECT_DOUBLE_EQ(rec.runningStatus.max(), 50.0);

    rec.appendRunningStatToStatfile();
    EXPECT_NEAR(rec.runningStatus.avg(), 30.0, 0.01);
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
    auto newStat = std::make_shared<FDRStore>(newStatPath, "JSON",
                                              STORE_WRITER);

    rec.ResetLogStatStorePtrs(newLog, newStat);
    EXPECT_EQ(rec.fdrLogReaderWriter, newLog);
    EXPECT_EQ(rec.fdrStatwriter, newStat);
}

// --- Store() EveryFetch policy ---

TEST_F(RecordTest, StoreEveryFetchStoresWhenReady)
{
    info.StorePolicy = "EveryFetch";
    info.FetchFreqSecs = 0;
    auto rec = makeRecord();
    rec.data.paramtype = "Uint64";
    rec.data.fdr_sample_data.set_paramvalueint64(42);
    rec.data.fdr_sample_data.set_paramid(42);
    rec.data.fdr_sample_data.set_timestamp(std::time(nullptr));
    rec.LastFetchedAt = std::time(nullptr);

    rec.Store();

    FDRStore reader(tmpDir + "/log.json", "JSON", STORE_READER);
    fdrpb::fdr_sample out;
    EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ);
    EXPECT_EQ(out.paramvalueint64(), 42);
}

TEST_F(RecordTest, StoreEveryFetchSkipsWhenTooRecent)
{
    info.StorePolicy = "EveryFetch";
    info.FetchFreqSecs = 9999;
    auto rec = makeRecord();
    rec.LastFetchedAt = std::time(nullptr);
    rec.LastStoredAt = std::time(nullptr);

    rec.Store();

    FDRStore reader(tmpDir + "/log.json", "JSON", STORE_READER);
    fdrpb::fdr_sample out;
    EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ_EOF);
}

// --- Store() Periodic policy ---

TEST_F(RecordTest, StorePeriodicStoresWhenReady)
{
    info.StorePolicy = "Periodic";
    info.StoreFreqSecs = 0;
    auto rec = makeRecord();
    rec.data.paramtype = "Uint64";
    rec.data.fdr_sample_data.set_paramvalueint64(77);
    rec.data.fdr_sample_data.set_paramid(42);
    rec.data.fdr_sample_data.set_timestamp(std::time(nullptr));

    rec.Store();

    FDRStore reader(tmpDir + "/log.json", "JSON", STORE_READER);
    fdrpb::fdr_sample out;
    EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ);
    EXPECT_EQ(out.paramvalueint64(), 77);
}

TEST_F(RecordTest, StorePeriodicSkipsWhenTooRecent)
{
    info.StorePolicy = "Periodic";
    info.StoreFreqSecs = 9999;
    auto rec = makeRecord();
    rec.LastStoredAt = std::time(nullptr);

    rec.Store();

    FDRStore reader(tmpDir + "/log.json", "JSON", STORE_READER);
    fdrpb::fdr_sample out;
    EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ_EOF);
}

// --- Store() OnChange policy ---

TEST_F(RecordTest, StoreOnChangeSkipsSameValue)
{
    info.StorePolicy = "OnChange";
    auto rec = makeRecord();
    rec.data.paramtype = "Uint64";
    rec.data.fdr_sample_data.set_paramvalueint64(100);
    rec.data.fdr_sample_data.set_paramid(42);
    rec.data.fdr_sample_data.set_timestamp(std::time(nullptr));

    rec.Store();
    rec.Store();

    FDRStore reader(tmpDir + "/log.json", "JSON", STORE_READER);
    fdrpb::fdr_sample out;
    int count = 0;
    while (reader.readnext(&out) == FDR_SUCCESS_DATA_READ)
    {
        count++;
    }
    EXPECT_EQ(count, 1);
}

// --- Store() updates LastStoredAt ---

TEST_F(RecordTest, StoreUpdatesLastStoredAt)
{
    info.StorePolicy = "EveryFetch";
    info.FetchFreqSecs = 0;
    auto rec = makeRecord();
    rec.data.paramtype = "Uint64";
    rec.data.fdr_sample_data.set_paramvalueint64(1);
    rec.data.fdr_sample_data.set_paramid(42);
    rec.data.fdr_sample_data.set_timestamp(std::time(nullptr));
    rec.LastFetchedAt = std::time(nullptr);

    EXPECT_EQ(rec.LastStoredAt, 0);
    rec.Store();
    EXPECT_GT(rec.LastStoredAt, 0);
}

// --- refreshDataCallback() ---

TEST_F(RecordTest, RefreshDataCallbackString)
{
    info.DataType = "string";
    auto rec = makeRecord();

    PropertyVariant val = std::string("test_value");
    rec.refreshDataCallback(val);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvaluestring(), "test_value");
    EXPECT_EQ(rec.data.fdr_sample_data.paramid(), 42u);
    EXPECT_GT(rec.LastFetchedAt, 0);
}

TEST_F(RecordTest, RefreshDataCallbackUint64)
{
    auto rec = makeRecord();

    PropertyVariant val = std::uint64_t(12345);
    rec.refreshDataCallback(val);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 12345);
    EXPECT_GT(rec.LastFetchedAt, 0);
}

// --- Refresh() skip check ---

TEST_F(RecordTest, RefreshSkipsWhenTooRecentAndNotViaTimer)
{
    info.FetchMethod = "Command";
    info.CommandParams.Command = "echo 42";
    info.DataType = "Uint64";
    info.FetchFreqSecs = 9999;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();
    rec.LastFetchedAt = std::time(nullptr);

    rec.Refresh(false);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 0);
}

// --- Refresh() Command path ---

TEST_F(RecordTest, RefreshCommandUint64)
{
    info.FetchMethod = "Command";
    info.CommandParams.Command = "echo 42";
    info.DataType = "Uint64";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 42u);
}

TEST_F(RecordTest, RefreshCommandString)
{
    info.FetchMethod = "Command";
    info.CommandParams.Command = "echo hello";
    info.DataType = "string";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    rec.Refresh(true);

    EXPECT_THAT(rec.data.fdr_sample_data.paramvaluestring(),
                HasSubstr("hello"));
}

TEST_F(RecordTest, RefreshCommandFailedReturnsEarly)
{
    info.FetchMethod = "Command";
    info.CommandParams.Command = "false";
    info.DataType = "Uint64";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 0);
}

TEST_F(RecordTest, RefreshCommandDoubleLogsWarning)
{
    info.FetchMethod = "Command";
    info.CommandParams.Command = "echo 3.14";
    info.DataType = "Double";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    EXPECT_NO_THROW(rec.Refresh(true));
}

TEST_F(RecordTest, RefreshCommandTriggersCompaction)
{
    info.FetchMethod = "Command";
    info.CommandParams.Command = "echo 42";
    info.DataType = "Uint64";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "Average";
    auto rec = makeRecord();

    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 42u);
    EXPECT_EQ(rec.runningStatus.numsamples(), 1);
}

// --- Refresh() unknown FetchMethod ---

TEST_F(RecordTest, RefreshUnknownFetchMethodReturnsEarly)
{
    info.FetchMethod = "UnknownMethod";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    EXPECT_NO_THROW(rec.Refresh(true));
}

// --- Refresh() Shmem path returns early ---

TEST_F(RecordTest, RefreshShmemReturnsEarly)
{
    info.FetchMethod = "Shmem";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 0);
}

// --- Refresh() DBUS path with variant types ---

TEST_F(RecordTest, RefreshDbusUint64FromUint64)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Uint64";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = uint64_t(42);
    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 42u);
}

TEST_F(RecordTest, RefreshDbusUint64FromDouble)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Uint64";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = double(99.9);
    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 99u);
}

TEST_F(RecordTest, RefreshDbusUint64FromUint32)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Uint64";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = uint32_t(200);
    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 200u);
}

TEST_F(RecordTest, RefreshDbusUint64FromUint16)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Uint64";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = uint16_t(300);
    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 300u);
}

TEST_F(RecordTest, RefreshDbusUint64FromInt64)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Int64";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = int64_t(500);
    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 500u);
}

TEST_F(RecordTest, RefreshDbusUint64FromBool)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Uint64";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = true;
    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 1u);
}

TEST_F(RecordTest, RefreshDbusUint64FromTupleBoolUint)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Uint64";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = std::tuple<bool, uint32_t>(true, 777);
    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 777u);
}

TEST_F(RecordTest, RefreshDbusUint64FromUint8)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Uint8";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = uint8_t(55);
    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 55u);
}

TEST_F(RecordTest, RefreshDbusUint64FromInt16)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Int16";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = int16_t(123);
    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 123u);
}

TEST_F(RecordTest, RefreshDbusDoubleFromDouble)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Double";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = double(3.14);
    rec.Refresh(true);

    EXPECT_NEAR(rec.data.fdr_sample_data.paramvaluedouble(), 3.14, 0.001);
}

TEST_F(RecordTest, RefreshDbusFloatFromUint32)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Float";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = uint32_t(42);
    rec.Refresh(true);

    EXPECT_DOUBLE_EQ(rec.data.fdr_sample_data.paramvaluedouble(), 42.0);
}

TEST_F(RecordTest, RefreshDbusStringFromString)
{
    info.FetchMethod = "DBUS";
    info.DataType = "string";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = std::string("dbus_value");
    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvaluestring(), "dbus_value");
}

TEST_F(RecordTest, RefreshDbusUint64UnknownVariantReturnsEarly)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Uint64";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = std::string("not_a_number");
    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 0);
}

TEST_F(RecordTest, RefreshDbusWithCompaction)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Uint64";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "Average";
    auto rec = makeRecord();

    g_stubDbusReturnValue = uint64_t(100);
    rec.Refresh(true);

    EXPECT_EQ(rec.runningStatus.numsamples(), 1);
}

// --- Refresh() DBUS_DGD path ---

TEST_F(RecordTest, RefreshDbusDgdReadsValue)
{
    info.FetchMethod = "DBUS_DGD";
    info.DataType = "Uint64";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusDGDReturnValue = std::make_tuple(0, std::string{}, uint64_t{99});
    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 99u);
}

// --- Refresh() DBUS_PT path ---

TEST_F(RecordTest, RefreshDbusPtReadsValue)
{
    info.FetchMethod = "DBUS_PT";
    info.DataType = "Uint64";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusPTReturnValue = std::make_tuple(0, uint64_t{88});
    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 88u);
}

// --- RefreshValue() unknown paramtype ---

TEST_F(RecordTest, RefreshValueUnknownParamTypeDoesNotCrash)
{
    info.DataType = "UnknownType";
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    EXPECT_NO_THROW(rec.RefreshValue("42"));
}

// --- RefreshValue with compaction ---

TEST_F(RecordTest, RefreshValueUint64WithCompaction)
{
    info.DataType = "Uint64";
    infogroup.CompactionMethod = "Average";
    auto rec = makeRecord();

    rec.RefreshValue("100");
    EXPECT_EQ(rec.runningStatus.numsamples(), 1);
    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 100);
}

// --- Refresh() DBUS Double/Float — all variant sub-types ---

TEST_F(RecordTest, RefreshDbusDoubleFromUint16)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Double";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = uint16_t(1234);
    rec.Refresh(true);

    EXPECT_DOUBLE_EQ(rec.data.fdr_sample_data.paramvaluedouble(), 1234.0);
}

TEST_F(RecordTest, RefreshDbusDoubleFromUint64)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Double";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = uint64_t(5000);
    rec.Refresh(true);

    EXPECT_DOUBLE_EQ(rec.data.fdr_sample_data.paramvaluedouble(), 5000.0);
}

TEST_F(RecordTest, RefreshDbusDoubleFromInt64)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Double";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = int64_t(-500);
    rec.Refresh(true);

    EXPECT_DOUBLE_EQ(rec.data.fdr_sample_data.paramvaluedouble(), -500.0);
}

TEST_F(RecordTest, RefreshDbusDoubleFromBool)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Double";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = true;
    rec.Refresh(true);

    EXPECT_DOUBLE_EQ(rec.data.fdr_sample_data.paramvaluedouble(), 1.0);
}

TEST_F(RecordTest, RefreshDbusDoubleFromTupleBoolUint)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Double";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = std::tuple<bool, uint32_t>(true, 256);
    rec.Refresh(true);

    EXPECT_DOUBLE_EQ(rec.data.fdr_sample_data.paramvaluedouble(), 256.0);
}

TEST_F(RecordTest, RefreshDbusDoubleFromUint8)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Double";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = uint8_t(77);
    rec.Refresh(true);

    EXPECT_DOUBLE_EQ(rec.data.fdr_sample_data.paramvaluedouble(), 77.0);
}

TEST_F(RecordTest, RefreshDbusDoubleFromInt16)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Double";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = int16_t(-300);
    rec.Refresh(true);

    EXPECT_DOUBLE_EQ(rec.data.fdr_sample_data.paramvaluedouble(), -300.0);
}

TEST_F(RecordTest, RefreshDbusDoubleUnknownVariantReturnsEarly)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Double";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = std::string("not_a_number");
    rec.Refresh(true);

    EXPECT_DOUBLE_EQ(rec.data.fdr_sample_data.paramvaluedouble(), 0.0);
}

// --- Refresh() DBUS with Float datatype ---

TEST_F(RecordTest, RefreshDbusFloatFromDouble)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Float";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = double(2.718);
    rec.Refresh(true);

    EXPECT_NEAR(rec.data.fdr_sample_data.paramvaluedouble(), 2.718, 0.001);
}

TEST_F(RecordTest, RefreshDbusFloatFromUint16)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Float";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = uint16_t(333);
    rec.Refresh(true);

    EXPECT_DOUBLE_EQ(rec.data.fdr_sample_data.paramvaluedouble(), 333.0);
}

TEST_F(RecordTest, RefreshDbusFloatFromUint64)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Float";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = uint64_t(9999);
    rec.Refresh(true);

    EXPECT_DOUBLE_EQ(rec.data.fdr_sample_data.paramvaluedouble(), 9999.0);
}

TEST_F(RecordTest, RefreshDbusFloatFromInt64)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Float";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = int64_t(-7777);
    rec.Refresh(true);

    EXPECT_DOUBLE_EQ(rec.data.fdr_sample_data.paramvaluedouble(), -7777.0);
}

TEST_F(RecordTest, RefreshDbusFloatFromBool)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Float";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = false;
    rec.Refresh(true);

    EXPECT_DOUBLE_EQ(rec.data.fdr_sample_data.paramvaluedouble(), 0.0);
}

TEST_F(RecordTest, RefreshDbusFloatFromTupleBoolUint)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Float";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = std::tuple<bool, uint32_t>(true, 512);
    rec.Refresh(true);

    EXPECT_DOUBLE_EQ(rec.data.fdr_sample_data.paramvaluedouble(), 512.0);
}

TEST_F(RecordTest, RefreshDbusFloatFromUint8)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Float";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = uint8_t(11);
    rec.Refresh(true);

    EXPECT_DOUBLE_EQ(rec.data.fdr_sample_data.paramvaluedouble(), 11.0);
}

TEST_F(RecordTest, RefreshDbusFloatFromInt16)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Float";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = int16_t(1024);
    rec.Refresh(true);

    EXPECT_DOUBLE_EQ(rec.data.fdr_sample_data.paramvaluedouble(), 1024.0);
}

TEST_F(RecordTest, RefreshDbusFloatUnknownVariantReturnsEarly)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Float";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = std::string("not_double");
    rec.Refresh(true);

    EXPECT_DOUBLE_EQ(rec.data.fdr_sample_data.paramvaluedouble(), 0.0);
}

// --- Refresh() DBUS with Integer/Uint32/Uint16 data types ---

TEST_F(RecordTest, RefreshDbusIntegerFromDouble)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Integer";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = double(88.8);
    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 88u);
}

TEST_F(RecordTest, RefreshDbusUint32FromUint32)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Uint32";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = uint32_t(4444);
    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 4444u);
}

TEST_F(RecordTest, RefreshDbusUint16FromUint16)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Uint16";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();

    g_stubDbusReturnValue = uint16_t(333);
    rec.Refresh(true);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 333u);
}

// --- Refresh() with DBUS and compaction (Double) ---

TEST_F(RecordTest, RefreshDbusDoubleWithCompaction)
{
    info.FetchMethod = "DBUS";
    info.DataType = "Double";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "Average";
    auto rec = makeRecord();

    g_stubDbusReturnValue = double(3.14);
    rec.Refresh(true);

    EXPECT_EQ(rec.runningStatus.numsamples(), 1);
}

// --- Refresh() timing: not-via-timer with enough elapsed time ---

TEST_F(RecordTest, RefreshNotViaTimerProceedsWhenReady)
{
    info.FetchMethod = "Command";
    info.CommandParams.Command = "echo 77";
    info.DataType = "Uint64";
    info.FetchFreqSecs = 0;
    infogroup.CompactionMethod = "";
    auto rec = makeRecord();
    rec.LastFetchedAt = 0;

    rec.Refresh(false);

    EXPECT_EQ(rec.data.fdr_sample_data.paramvalueint64(), 77u);
}

// --- RunningStatisticEngine with Double paramtype ---

TEST_F(RecordTest, RunningStatDoubleParamtype)
{
    auto rec = makeRecord();
    rec.data.paramtype = "Double";

    fdrpb::fdr_sample s;
    s.set_timestamp(1000);
    s.set_paramid(42);
    s.set_paramvaluedouble(3.14159);

    rec.RunningStatisticEngine(s);

    EXPECT_EQ(rec.runningStatus.numsamples(), 1);
    EXPECT_NEAR(rec.runningStatus.min(), 3.142, 0.001);
    EXPECT_NEAR(rec.runningStatus.max(), 3.142, 0.001);
}

TEST_F(RecordTest, RunningStatMinMaxTimestampsUpdate)
{
    auto rec = makeRecord();
    rec.data.paramtype = "Uint64";

    rec.RunningStatisticEngine(makeSample(1000, 42, 50));
    rec.RunningStatisticEngine(makeSample(2000, 42, 10));
    rec.RunningStatisticEngine(makeSample(3000, 42, 90));

    EXPECT_EQ(rec.runningStatus.numsamples(), 3);
    EXPECT_DOUBLE_EQ(rec.runningStatus.min(), 10.0);
    EXPECT_EQ(rec.runningStatus.minvaltimestamp(), 2000u);
    EXPECT_DOUBLE_EQ(rec.runningStatus.max(), 90.0);
    EXPECT_EQ(rec.runningStatus.maxvaltimestamp(), 3000u);
    EXPECT_EQ(rec.runningStatus.fromtime(), 1000u);
    EXPECT_EQ(rec.runningStatus.totime(), 3000u);
}

TEST_F(RecordTest, RunningStatFromTimeSetOnce)
{
    auto rec = makeRecord();
    rec.data.paramtype = "Uint64";

    rec.RunningStatisticEngine(makeSample(5000, 42, 1));
    rec.RunningStatisticEngine(makeSample(6000, 42, 2));

    EXPECT_EQ(rec.runningStatus.fromtime(), 5000u);
    EXPECT_EQ(rec.runningStatus.totime(), 6000u);
}

// --- appendRunningStatToStatfile with zero samples ---

TEST_F(RecordTest, AppendRunningStatZeroSamplesNoOp)
{
    auto rec = makeRecord();
    EXPECT_EQ(rec.runningStatus.numsamples(), 0);

    rec.appendRunningStatToStatfile();

    FDRStore reader(tmpDir + "/stat.json", "JSON", STORE_READER);
    fdrpb::fdr_stat out;
    EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ_EOF);
}

TEST_F(RecordTest, AppendRunningStatCorrectAverage)
{
    auto rec = makeRecord();
    rec.data.paramtype = "Uint64";

    rec.RunningStatisticEngine(makeSample(1000, 42, 10));
    rec.RunningStatisticEngine(makeSample(2000, 42, 20));
    rec.RunningStatisticEngine(makeSample(3000, 42, 30));

    rec.appendRunningStatToStatfile();

    EXPECT_NEAR(rec.runningStatus.avg(), 20.0, 0.01);

    FDRStore reader(tmpDir + "/stat.json", "JSON", STORE_READER);
    fdrpb::fdr_stat out;
    EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ);
    EXPECT_EQ(out.numsamples(), 3);
    EXPECT_NEAR(out.avg(), 20.0, 0.01);
}

// --- Store() OnChange with different values ---

TEST_F(RecordTest, StoreOnChangeDifferentValuesStoresBoth)
{
    info.StorePolicy = "OnChange";
    auto rec = makeRecord();
    rec.data.paramtype = "Uint64";
    rec.data.fdr_sample_data.set_paramvalueint64(100);
    rec.data.fdr_sample_data.set_paramid(42);
    rec.data.fdr_sample_data.set_timestamp(std::time(nullptr));

    rec.Store();

    rec.data.fdr_sample_data.set_paramvalueint64(200);
    rec.Store();

    FDRStore reader(tmpDir + "/log.json", "JSON", STORE_READER);
    fdrpb::fdr_sample out;
    int count = 0;
    while (reader.readnext(&out) == FDR_SUCCESS_DATA_READ)
    {
        count++;
    }
    EXPECT_EQ(count, 2);
}

// --- Store() OnChange with string type ---

TEST_F(RecordTest, StoreOnChangeStringType)
{
    info.StorePolicy = "OnChange";
    info.DataType = "string";
    auto rec = makeRecord();
    rec.data.paramtype = "string";
    rec.data.fdr_sample_data.set_paramvaluestring("hello");
    rec.data.fdr_sample_data.set_paramid(42);
    rec.data.fdr_sample_data.set_timestamp(std::time(nullptr));

    rec.Store();
    rec.Store();

    FDRStore reader(tmpDir + "/log.json", "JSON", STORE_READER);
    fdrpb::fdr_sample out;
    int count = 0;
    while (reader.readnext(&out) == FDR_SUCCESS_DATA_READ)
    {
        count++;
    }
    EXPECT_EQ(count, 1);
}

// --- same_data_values() free function ---

TEST_F(RecordTest, SameDataValuesDifferentParamIdReturnsFalse)
{
    fdr_sample_ext a, b;
    a.paramtype = "Uint64";
    b.paramtype = "Uint64";
    a.fdr_sample_data.set_paramid(1);
    b.fdr_sample_data.set_paramid(2);
    a.fdr_sample_data.set_paramvalueint64(100);
    b.fdr_sample_data.set_paramvalueint64(100);

    extern bool same_data_values(const fdr_sample_ext& left,
                                 const fdr_sample_ext& right);
    EXPECT_FALSE(same_data_values(a, b));
}

TEST_F(RecordTest, SameDataValuesDifferentParamTypeReturnsFalse)
{
    fdr_sample_ext a, b;
    a.paramtype = "Uint64";
    b.paramtype = "string";
    a.fdr_sample_data.set_paramid(1);
    b.fdr_sample_data.set_paramid(1);

    extern bool same_data_values(const fdr_sample_ext& left,
                                 const fdr_sample_ext& right);
    EXPECT_FALSE(same_data_values(a, b));
}

TEST_F(RecordTest, SameDataValuesMatchingUint64ReturnsTrue)
{
    fdr_sample_ext a, b;
    a.paramtype = "Uint64";
    b.paramtype = "Uint64";
    a.fdr_sample_data.set_paramid(1);
    b.fdr_sample_data.set_paramid(1);
    a.fdr_sample_data.set_paramvalueint64(42);
    b.fdr_sample_data.set_paramvalueint64(42);

    extern bool same_data_values(const fdr_sample_ext& left,
                                 const fdr_sample_ext& right);
    EXPECT_TRUE(same_data_values(a, b));
}

TEST_F(RecordTest, SameDataValuesDifferentUint64ReturnsFalse)
{
    fdr_sample_ext a, b;
    a.paramtype = "Uint64";
    b.paramtype = "Uint64";
    a.fdr_sample_data.set_paramid(1);
    b.fdr_sample_data.set_paramid(1);
    a.fdr_sample_data.set_paramvalueint64(42);
    b.fdr_sample_data.set_paramvalueint64(99);

    extern bool same_data_values(const fdr_sample_ext& left,
                                 const fdr_sample_ext& right);
    EXPECT_FALSE(same_data_values(a, b));
}

TEST_F(RecordTest, SameDataValuesMatchingStringReturnsTrue)
{
    fdr_sample_ext a, b;
    a.paramtype = "string";
    b.paramtype = "string";
    a.fdr_sample_data.set_paramid(1);
    b.fdr_sample_data.set_paramid(1);
    a.fdr_sample_data.set_paramvaluestring("hello");
    b.fdr_sample_data.set_paramvaluestring("hello");

    extern bool same_data_values(const fdr_sample_ext& left,
                                 const fdr_sample_ext& right);
    EXPECT_TRUE(same_data_values(a, b));
}

TEST_F(RecordTest, SameDataValuesDifferentStringReturnsFalse)
{
    fdr_sample_ext a, b;
    a.paramtype = "string";
    b.paramtype = "string";
    a.fdr_sample_data.set_paramid(1);
    b.fdr_sample_data.set_paramid(1);
    a.fdr_sample_data.set_paramvaluestring("hello");
    b.fdr_sample_data.set_paramvaluestring("world");

    extern bool same_data_values(const fdr_sample_ext& left,
                                 const fdr_sample_ext& right);
    EXPECT_FALSE(same_data_values(a, b));
}

// --- print_data() free function ---

TEST_F(RecordTest, PrintDataUint64DoesNotCrash)
{
    fdr_sample_ext d;
    d.paramtype = "Uint64";
    d.fdr_sample_data.set_paramid(1);
    d.fdr_sample_data.set_paramvalueint64(42);

    extern void print_data(const std::string name, const fdr_sample_ext& dat);
    EXPECT_NO_THROW(print_data("TestParam", d));
}

TEST_F(RecordTest, PrintDataStringDoesNotCrash)
{
    fdr_sample_ext d;
    d.paramtype = "string";
    d.fdr_sample_data.set_paramid(2);
    d.fdr_sample_data.set_paramvaluestring("test_val");

    extern void print_data(const std::string name, const fdr_sample_ext& dat);
    EXPECT_NO_THROW(print_data("TestParam", d));
}

// --- print_fdrStatwriterStoragefilepath ---

TEST_F(RecordTest, PrintFdrStatwriterStoragefilepath)
{
    auto rec = makeRecord();
    EXPECT_EQ(rec.print_fdrStatwriterStoragefilepath(), tmpDir + "/stat.json");
}
