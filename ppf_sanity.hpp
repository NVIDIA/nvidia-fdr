/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#pragma once

class PPFSanity
{
  private:
    std::map<std::string, std::vector<InfoGroup_t>>
        CreateComponentsMap(const YAML::Node& PlatformProfile);
    int ValidatePPFComponents(
        const std::map<std::string, std::vector<InfoGroup_t>>& ComponentsMap);
    template <typename T>
    int UniqueParamChecker(const T& paramValue, std::string paramName,
                           std::string paramClass, std::string component);

  public:
    int SanityTestPPF(const std::string PPFName);
};
