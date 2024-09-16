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

#include <string>
#include <unordered_map>
#include <vector>

#include "fdr_record.cpp"

// List of records which belong to the same FetchMethod
//  Group Key and Records
#define GROUP_KEY_METHOD_POS 0
#define GROUP_KEY_NAMESPACE_POS 1
#define MIN_GROUP_KEY_SIZE 2

class FdrGrpUpdate
{
  public:
    static void RefreshAndStore(const std::vector<std::string>& keys,
                                const std::vector<Record*>& records);
};
