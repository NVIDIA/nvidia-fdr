/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for ppf_sanity.cpp — PPFSanity class
 *
 * Covers:
 *   - SanityTestPPF: valid PPF, duplicate ParamID, duplicate Info ID
 *   - Non-existent file, malformed YAML, empty file
 *   - Multiple independent components
 *   - Example PPF validation
 */

#include "fdr_common.hpp"
#include "ppf_sanity.hpp"
#include "testCommon.hpp"

#include <yaml-cpp/yaml.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class PPFSanityTest : public Test
{
  protected:
    std::string tmpDir;

    void SetUp() override
    {
        auto uniquePath =
            fs::temp_directory_path() /
            ("ppf_sanity_test_" +
             std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(uniquePath);
        tmpDir = uniquePath.string();
    }

    void TearDown() override
    {
        fs::remove_all(tmpDir);
    }

    std::string writeYaml(const std::string& name, const std::string& content)
    {
        std::string path = tmpDir + "/" + name;
        std::ofstream ofs(path);
        if (!ofs.is_open())
        {
            ADD_FAILURE() << "Failed to open file: " << path;
            return {};
        }
        ofs << content;
        ofs.close();
        if (ofs.fail())
        {
            ADD_FAILURE() << "Failed to write/flush file: " << path;
            return {};
        }
        return path;
    }
};

TEST_F(PPFSanityTest, ValidPPFNoDuplicates)
{
    auto path = writeYaml("valid.yaml", R"(
FingerPrint:
  Checks: []
GeneralConfig:
  LogsBasePath:
    - "/tmp/fdr/"
  LogsFormat: JSON
Preconditions:
  Threshold: 10
ValidComp_A1:
  InfoGroups:
    - ID: Inventory
      InfoList:
        - ID: Model
          ParamID: 0
          DataType: string
        - ID: Serial
          ParamID: 1
          DataType: string
    - ID: Stats
      InfoList:
        - ID: Temp
          ParamID: 0
          DataType: Uint64
)");
    PPFSanity checker;
    EXPECT_EQ(checker.SanityTestPPF(path), FDR_SUCCESS);
}

TEST_F(PPFSanityTest, DuplicateParamIDDetected)
{
    auto path = writeYaml("dup_param.yaml", R"(
DupParamComp_B1:
  InfoGroups:
    - ID: Sensors
      InfoList:
        - ID: Metric1
          ParamID: 5
          DataType: string
        - ID: Metric2
          ParamID: 5
          DataType: string
)");
    PPFSanity checker;
    EXPECT_NE(checker.SanityTestPPF(path), FDR_SUCCESS);
}

TEST_F(PPFSanityTest, DuplicateInfoIDDetected)
{
    auto path = writeYaml("dup_id.yaml", R"(
DupIDComp_C1:
  InfoGroups:
    - ID: Inventory
      InfoList:
        - ID: SameName
          ParamID: 0
          DataType: string
        - ID: SameName
          ParamID: 1
          DataType: string
)");
    PPFSanity checker;
    EXPECT_NE(checker.SanityTestPPF(path), FDR_SUCCESS);
}

TEST_F(PPFSanityTest, NonExistentFileReturnsFailure)
{
    PPFSanity checker;
    EXPECT_EQ(checker.SanityTestPPF("/no/such/file.yaml"), EXIT_FAILURE);
}

TEST_F(PPFSanityTest, MalformedYamlReturnsFailure)
{
    auto path = writeYaml("bad.yaml", "{{{{not valid yaml: ][");
    PPFSanity checker;
    EXPECT_EQ(checker.SanityTestPPF(path), EXIT_FAILURE);
}

TEST_F(PPFSanityTest, EmptyFileReturnsSuccess)
{
    auto path = writeYaml("empty.yaml", "");
    PPFSanity checker;
    EXPECT_EQ(checker.SanityTestPPF(path), FDR_SUCCESS);
}

TEST_F(PPFSanityTest, MultipleComponentsIndependent)
{
    auto path = writeYaml("multi_comp.yaml", R"(
MultiComp_D1:
  InfoGroups:
    - ID: Stats
      InfoList:
        - ID: Alpha
          ParamID: 0
          DataType: string
MultiComp_D2:
  InfoGroups:
    - ID: Stats
      InfoList:
        - ID: Alpha
          ParamID: 0
          DataType: string
)");
    PPFSanity checker;
    EXPECT_EQ(checker.SanityTestPPF(path), FDR_SUCCESS);
}

TEST_F(PPFSanityTest, ExamplePPFIsValid)
{
    std::string examplePath = std::string(TEST_SOURCE_DIR) +
                              "/../platforms/fdr_ppf_example.yaml";
    if (!fs::exists(examplePath))
    {
        GTEST_SKIP() << "Example PPF not found at " << examplePath;
    }

    // yaml-cpp 0.8+ dropped YAML merge key (<<) resolution, which the
    // example PPF relies on.  Detect the library version at runtime and
    // skip when merge keys are unsupported.
    try
    {
        YAML::Node probe =
            YAML::Load("base: &base\n  k: v\nderived:\n  <<: *base\n");
        if (!probe["derived"]["k"].IsDefined())
        {
            GTEST_SKIP()
                << "yaml-cpp does not resolve merge keys (likely >= 0.8); "
                   "skipping example PPF validation";
        }
    }
    catch (...)
    {
        GTEST_SKIP() << "yaml-cpp merge-key probe failed; skipping";
    }

    PPFSanity checker;
    int rc = checker.SanityTestPPF(examplePath);
    EXPECT_EQ(rc, FDR_SUCCESS);
}
