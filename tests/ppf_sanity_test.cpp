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

#include "testCommon.hpp"

#include "ppf_sanity.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class PPFSanityTest : public Test
{
  protected:
    std::string tmpDir;

    void SetUp() override
    {
        tmpDir = fs::temp_directory_path() / "ppf_sanity_test_XXXXXX";
        tmpDir = std::string(mkdtemp(tmpDir.data()));
    }

    void TearDown() override
    {
        fs::remove_all(tmpDir);
    }

    std::string writeYaml(const std::string& name, const std::string& content)
    {
        std::string path = tmpDir + "/" + name;
        std::ofstream ofs(path);
        ofs << content;
        ofs.close();
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
    PPFSanity checker;
    std::string examplePath =
        std::string(TEST_SOURCE_DIR) + "/../platforms/fdr_ppf_example.yaml";
    if (fs::exists(examplePath))
    {
        EXPECT_EQ(checker.SanityTestPPF(examplePath), FDR_SUCCESS);
    }
    else
    {
        GTEST_SKIP() << "Example PPF not found at " << examplePath;
    }
}
