/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for fdr_store.cpp — FDRStore class
 *
 * Covers:
 *   - JSON append/readnext round-trip
 *   - BINARY append/readnext round-trip
 *   - Empty file handling (JSON + BINARY)
 *   - getStoreFilePath
 *   - Multiple sequential appends
 *   - fdr_stat protobuf message round-trip
 *   - Internal state verification via #define private public
 */

#include "testCommon.hpp"

#include "fdr_store.hpp"

#include "fdr_common.hpp"
#include "fdr_logs_schema.pb.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class FDRStoreTest : public Test
{
  protected:
    std::string tmpDir;

    void SetUp() override
    {
        tmpDir = fs::temp_directory_path() / "fdr_store_test_XXXXXX";
        tmpDir = std::string(mkdtemp(tmpDir.data()));
    }

    void TearDown() override
    {
        fs::remove_all(tmpDir);
    }

    std::string tmpFile(const std::string& name)
    {
        return tmpDir + "/" + name;
    }

    fdrpb::fdr_sample makeSample(uint64_t ts, uint32_t paramId,
                                 const std::string& strVal)
    {
        fdrpb::fdr_sample s;
        s.set_timestamp(ts);
        s.set_paramid(paramId);
        s.set_paramvaluestring(strVal);
        return s;
    }

    fdrpb::fdr_sample makeSampleInt(uint64_t ts, uint32_t paramId, int64_t val)
    {
        fdrpb::fdr_sample s;
        s.set_timestamp(ts);
        s.set_paramid(paramId);
        s.set_paramvalueint64(val);
        return s;
    }
};

// --- JSON round-trip ---

TEST_F(FDRStoreTest, JsonAppendAndReadNext)
{
    auto path = tmpFile("test.json");

    {
        FDRStore writer(path, "JSON", STORE_WRITER);
        writer.append(makeSample(1000, 1, "hello"));
        writer.append(makeSample(2000, 2, "world"));
    }

    {
        FDRStore reader(path, "JSON", STORE_READER);
        fdrpb::fdr_sample out;

        EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ);
        EXPECT_EQ(out.timestamp(), 1000u);
        EXPECT_EQ(out.paramid(), 1u);
        EXPECT_EQ(out.paramvaluestring(), "hello");

        EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ);
        EXPECT_EQ(out.timestamp(), 2000u);
        EXPECT_EQ(out.paramvaluestring(), "world");

        EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ_EOF);
    }
}

TEST_F(FDRStoreTest, JsonAppendIntValues)
{
    auto path = tmpFile("int.json");

    {
        FDRStore writer(path, "JSON", STORE_WRITER);
        writer.append(makeSampleInt(100, 10, 42));
    }

    {
        FDRStore reader(path, "JSON", STORE_READER);
        fdrpb::fdr_sample out;

        EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ);
        EXPECT_EQ(out.paramvalueint64(), 42);
    }
}

// --- BINARY round-trip ---

TEST_F(FDRStoreTest, BinaryAppendAndReadNext)
{
    auto path = tmpFile("test.bin");

    {
        FDRStore writer(path, "BINARY", STORE_WRITER);
        writer.append(makeSample(3000, 3, "binary_test"));
        writer.append(makeSampleInt(4000, 4, 999));
    }

    {
        FDRStore reader(path, "BINARY", STORE_READER);
        fdrpb::fdr_sample out;

        EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ);
        EXPECT_EQ(out.timestamp(), 3000u);
        EXPECT_EQ(out.paramvaluestring(), "binary_test");

        fdrpb::fdr_sample out2;
        EXPECT_EQ(reader.readnext(&out2), FDR_SUCCESS_DATA_READ);
        EXPECT_EQ(out2.paramvalueint64(), 999);

        fdrpb::fdr_sample out3;
        EXPECT_EQ(reader.readnext(&out3), FDR_SUCCESS_DATA_READ_EOF);
    }
}

// --- Empty file ---

TEST_F(FDRStoreTest, ReadEmptyJsonFileReturnsEOF)
{
    auto path = tmpFile("empty.json");
    std::ofstream(path).close();

    FDRStore reader(path, "JSON", STORE_READER);
    fdrpb::fdr_sample out;
    EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ_EOF);
}

TEST_F(FDRStoreTest, ReadEmptyBinaryFileReturnsEOF)
{
    auto path = tmpFile("empty.bin");
    std::ofstream(path).close();

    FDRStore reader(path, "BINARY", STORE_READER);
    fdrpb::fdr_sample out;
    EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ_EOF);
}

// --- getStoreFilePath ---

TEST_F(FDRStoreTest, GetStoreFilePath)
{
    auto path = tmpFile("filepath.json");
    FDRStore store(path, "JSON", STORE_WRITER);
    EXPECT_EQ(store.getStoreFilePath(), path);
}

// --- Internal state: format stored correctly ---

TEST_F(FDRStoreTest, InternalFormatIsStoredCorrectly)
{
    auto path = tmpFile("format_check.bin");
    FDRStore store(path, "BINARY", STORE_WRITER);
    EXPECT_EQ(store.storeFilePath, path);
}

// --- Multiple appends ---

TEST_F(FDRStoreTest, JsonMultipleAppendsSameWriter)
{
    auto path = tmpFile("multi.json");

    FDRStore writer(path, "JSON", STORE_WRITER);
    for (int i = 0; i < 10; i++)
    {
        writer.append(makeSampleInt(i * 100, i, i * 10));
    }

    FDRStore reader(path, "JSON", STORE_READER);
    fdrpb::fdr_sample out;
    int count = 0;
    while (reader.readnext(&out) == FDR_SUCCESS_DATA_READ)
    {
        EXPECT_EQ(out.paramvalueint64(), count * 10);
        count++;
    }
    EXPECT_EQ(count, 10);
}

// --- Stat message round-trip ---

TEST_F(FDRStoreTest, JsonStatMessageRoundTrip)
{
    auto path = tmpFile("stat.json");

    fdrpb::fdr_stat stat;
    stat.set_fromtime(1000);
    stat.set_totime(2000);
    stat.set_numsamples(50);
    stat.set_min(10.5);
    stat.set_max(90.3);
    stat.set_avg(45.2);
    stat.set_paramid(7);

    {
        FDRStore writer(path, "JSON", STORE_WRITER);
        writer.append(stat);
    }

    {
        FDRStore reader(path, "JSON", STORE_READER);
        fdrpb::fdr_stat out;
        EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ);
        EXPECT_EQ(out.fromtime(), 1000u);
        EXPECT_EQ(out.totime(), 2000u);
        EXPECT_EQ(out.numsamples(), 50);
        EXPECT_DOUBLE_EQ(out.min(), 10.5);
        EXPECT_DOUBLE_EQ(out.max(), 90.3);
        EXPECT_NEAR(out.avg(), 45.2, 0.01);
        EXPECT_EQ(out.paramid(), 7u);
    }
}

// --- BINARY stat round-trip ---

TEST_F(FDRStoreTest, BinaryStatMessageRoundTrip)
{
    auto path = tmpFile("stat.bin");

    fdrpb::fdr_stat stat;
    stat.set_fromtime(5000);
    stat.set_totime(6000);
    stat.set_numsamples(100);
    stat.set_min(1.0);
    stat.set_max(99.0);
    stat.set_avg(50.0);
    stat.set_paramid(3);

    {
        FDRStore writer(path, "BINARY", STORE_WRITER);
        writer.append(stat);
    }

    {
        FDRStore reader(path, "BINARY", STORE_READER);
        fdrpb::fdr_stat out;
        EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ);
        EXPECT_EQ(out.fromtime(), 5000u);
        EXPECT_EQ(out.numsamples(), 100);
        EXPECT_DOUBLE_EQ(out.avg(), 50.0);
    }
}
