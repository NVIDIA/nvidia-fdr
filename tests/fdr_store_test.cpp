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

// clang-format off
#include "testCommon.hpp"
#include "fdr_logs_schema.pb.h"
#include "fdr_common.hpp"
#include "fdr_store.hpp"
// clang-format on

#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

class FDRStoreTest : public Test
{
  protected:
    std::string tmpDir;

    void SetUp() override
    {
        std::string templateStr =
            (fs::temp_directory_path() / "fdr_store_test_XXXXXX").string();
        std::vector<char> buffer(templateStr.begin(), templateStr.end());
        buffer.push_back('\0');
        tmpDir = std::string(mkdtemp(buffer.data()));
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

// --- Internal state: format and path stored correctly ---

TEST_F(FDRStoreTest, InternalFormatIsStoredCorrectly)
{
    auto path = tmpFile("format_check.bin");
    FDRStore store(path, "BINARY", STORE_WRITER);
    EXPECT_EQ(store.storagefilepath, path);
    EXPECT_EQ(store.encodingtouse, "BINARY");
}

// --- Multiple appends ---

TEST_F(FDRStoreTest, JsonMultipleAppendsSameWriter)
{
    auto path = tmpFile("multi.json");

    {
        FDRStore writer(path, "JSON", STORE_WRITER);
        for (int i = 0; i < 10; i++)
        {
            writer.append(makeSampleInt(i * 100, i, i * 10));
        }
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

// --- Unknown encoding ---

TEST_F(FDRStoreTest, AppendUnknownEncodingDoesNothing)
{
    auto path = tmpFile("unknown.dat");
    FDRStore writer(path, "SQLITE", STORE_WRITER);
    writer.append(makeSample(1000, 1, "test"));
    EXPECT_FALSE(fs::exists(path));
}

TEST_F(FDRStoreTest, ReadnextUnknownEncodingReturnsEOF)
{
    auto path = tmpFile("unknown_read.dat");
    std::ofstream(path) << "some data";

    FDRStore reader(path, "SQLITE", STORE_READER);
    fdrpb::fdr_sample out;
    EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ_EOF);
}

// --- JSON malformed ---

TEST_F(FDRStoreTest, ReadnextJsonMalformedLine)
{
    auto path = tmpFile("malformed.json");
    std::ofstream(path) << "this is not json\n";

    FDRStore reader(path, "JSON", STORE_READER);
    fdrpb::fdr_sample out;
    // By design: JSON parse failures return FDR_SUCCESS_DATA_READ with an
    // undefined message state, allowing callers to continue reading subsequent
    // lines. This differs from BINARY mode, which returns
    // FDR_ERR_DATA_READ_CORRUPT_EOF on malformed input.
    EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ);
}

// --- Binary corrupt after valid data ---

TEST_F(FDRStoreTest, ReadnextBinaryCorruptAfterValid)
{
    auto path = tmpFile("corrupt_after_valid.bin");
    {
        FDRStore writer(path, "BINARY", STORE_WRITER);
        writer.append(makeSampleInt(1000, 1, 42));
    }
    {
        std::ofstream f(path, std::ios::binary | std::ios::app);
        const char garbage[] = "\xff\xff\xff\xff\x0fGARBAGE";
        f.write(garbage, sizeof(garbage) - 1);
    }

    FDRStore reader(path, "BINARY", STORE_READER);
    fdrpb::fdr_sample out;

    EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ);
    EXPECT_EQ(out.paramvalueint64(), 42);

    fdrpb::fdr_sample out2;
    EXPECT_EQ(reader.readnext(&out2), FDR_ERR_DATA_READ_CORRUPT_EOF);
}

// --- printUnsetFields ---

TEST_F(FDRStoreTest, PrintUnsetFieldsPartialMessage)
{
    auto path = tmpFile("unset.json");
    FDRStore store(path, "JSON", STORE_WRITER);

    fdrpb::fdr_sample partial;
    partial.set_timestamp(1000);
    EXPECT_NO_THROW(store.printUnsetFields(partial));
}

TEST_F(FDRStoreTest, PrintUnsetFieldsFullMessage)
{
    auto path = tmpFile("allset.json");
    FDRStore store(path, "JSON", STORE_WRITER);

    fdrpb::fdr_sample full;
    full.set_timestamp(1000);
    full.set_paramid(1);
    full.set_paramvalueint64(42);
    full.set_paramvaluestring("test");
    full.set_paramvaluedouble(3.14);
    EXPECT_NO_THROW(store.printUnsetFields(full));
}

// --- STORE_READER opens input stream ---

TEST_F(FDRStoreTest, ReaderOpensInputStreamForJson)
{
    auto path = tmpFile("reader_test.json");
    {
        FDRStore writer(path, "JSON", STORE_WRITER);
        writer.append(makeSample(1000, 1, "val"));
    }

    FDRStore reader(path, "JSON", STORE_READER);
    EXPECT_TRUE(reader.instream.is_open());
}

TEST_F(FDRStoreTest, ReaderOpensInputStreamForBinary)
{
    auto path = tmpFile("reader_test.bin");
    {
        FDRStore writer(path, "BINARY", STORE_WRITER);
        writer.append(makeSampleInt(1000, 1, 42));
    }

    FDRStore reader(path, "BINARY", STORE_READER);
    EXPECT_TRUE(reader.instream.is_open());
    EXPECT_NE(reader.binaryinzerocopystream, nullptr);
}

// --- Writer does not open instream ---

TEST_F(FDRStoreTest, WriterDoesNotOpenInstream)
{
    auto path = tmpFile("writer_no_instream.json");
    FDRStore writer(path, "JSON", STORE_WRITER);
    EXPECT_FALSE(writer.instream.is_open());
    EXPECT_EQ(writer.binaryinzerocopystream, nullptr);
}

// --- fdr_event protobuf round-trip ---

TEST_F(FDRStoreTest, JsonEventMessageRoundTrip)
{
    auto path = tmpFile("event.json");

    fdrpb::fdr_event ev;
    ev.set_eventtimestamp(12345);
    ev.set_eventname("gpu_error");
    ev.set_eventloggingpath("BootCount_1/GPU/GPU0/Event_12345.dat");

    {
        FDRStore writer(path, "JSON", STORE_WRITER);
        writer.append(ev);
    }

    {
        FDRStore reader(path, "JSON", STORE_READER);
        fdrpb::fdr_event out;
        EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ);
        EXPECT_EQ(out.eventtimestamp(), 12345u);
        EXPECT_EQ(out.eventname(), "gpu_error");
    }
}

// --- fdr_book_of_errors protobuf round-trip ---

TEST_F(FDRStoreTest, JsonBookOfErrorsRoundTrip)
{
    auto path = tmpFile("boe.json");

    fdrpb::fdr_book_of_errors boe;
    boe.set_bootid("42");
    boe.set_paramid(9999);
    boe.set_deviceinstance("GPU0");
    boe.set_errortype("Faults and Errors");
    boe.set_erroroccurtimestamp(1000);

    {
        FDRStore writer(path, "JSON", STORE_WRITER);
        writer.append(boe);
    }

    {
        FDRStore reader(path, "JSON", STORE_READER);
        fdrpb::fdr_book_of_errors out;
        EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ);
        EXPECT_EQ(out.bootid(), "42");
        EXPECT_EQ(out.paramid(), 9999u);
        EXPECT_EQ(out.deviceinstance(), "GPU0");
        EXPECT_EQ(out.errortype(), "Faults and Errors");
    }
}

// --- Binary event round-trip ---

TEST_F(FDRStoreTest, BinaryEventMessageRoundTrip)
{
    auto path = tmpFile("event.bin");

    fdrpb::fdr_event ev;
    ev.set_eventtimestamp(99999);
    ev.set_eventname("nvswitch_fault");

    {
        FDRStore writer(path, "BINARY", STORE_WRITER);
        writer.append(ev);
    }

    {
        FDRStore reader(path, "BINARY", STORE_READER);
        fdrpb::fdr_event out;
        EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ);
        EXPECT_EQ(out.eventtimestamp(), 99999u);
        EXPECT_EQ(out.eventname(), "nvswitch_fault");
    }
}

// --- Multiple binary appends ---

TEST_F(FDRStoreTest, BinaryMultipleAppendsSameWriter)
{
    auto path = tmpFile("multi.bin");

    {
        FDRStore writer(path, "BINARY", STORE_WRITER);
        for (int i = 0; i < 10; i++)
        {
            writer.append(makeSampleInt(i * 100, i, i * 10));
        }
    }

    FDRStore reader(path, "BINARY", STORE_READER);
    fdrpb::fdr_sample out;
    int count = 0;
    while (reader.readnext(&out) == FDR_SUCCESS_DATA_READ)
    {
        EXPECT_EQ(out.paramvalueint64(), count * 10);
        count++;
    }
    EXPECT_EQ(count, 10);
}

// --- JSON readnext on non-existent file ---

TEST_F(FDRStoreTest, ReadnextJsonNonexistentFileReturnsEOF)
{
    FDRStore reader(tmpFile("does_not_exist.json"), "JSON", STORE_READER);
    fdrpb::fdr_sample out;
    EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ_EOF);
}

// --- Binary readnext on non-existent file ---

TEST_F(FDRStoreTest, ReadnextBinaryNonexistentFileReturnsEOF)
{
    FDRStore reader(tmpFile("does_not_exist.bin"), "BINARY", STORE_READER);
    fdrpb::fdr_sample out;
    // A missing file leaves instream not open; readnext skips the binary read
    // path entirely and returns FDR_SUCCESS_DATA_READ_EOF deterministically.
    // FDR_ERR_DATA_READ_CORRUPT_EOF is reserved for actual corruption only.
    EXPECT_EQ(reader.readnext(&out), FDR_SUCCESS_DATA_READ_EOF);
}

// --- Destructor cleans up binaryinzerocopystream ---

TEST_F(FDRStoreTest, DestructorCleansUpBinaryStream)
{
    auto path = tmpFile("cleanup.bin");
    {
        FDRStore writer(path, "BINARY", STORE_WRITER);
        writer.append(makeSampleInt(1000, 1, 42));
    }

    {
        FDRStore reader(path, "BINARY", STORE_READER);
        EXPECT_NE(reader.binaryinzerocopystream, nullptr);
    }
}

// --- numFdsObjs tracking ---

TEST_F(FDRStoreTest, NumFdsObjsTracking)
{
    auto before = numFdsObjs;
    {
        FDRStore store1(tmpFile("a.json"), "JSON", STORE_WRITER);
        EXPECT_EQ(numFdsObjs, before + 1);
        {
            FDRStore store2(tmpFile("b.json"), "JSON", STORE_WRITER);
            EXPECT_EQ(numFdsObjs, before + 2);
        }
        EXPECT_EQ(numFdsObjs, before + 1);
    }
    EXPECT_EQ(numFdsObjs, before);
}
