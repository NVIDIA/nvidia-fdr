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

#include <ostream>
#include <string>
#include <stdlib.h>
#include <vector>
#include <iostream>

#define ONE_MB              1024UL*1024UL
#define BOOT_TIME_DIR_COUNT 1
#define RUN_TIME_DIR_COUNT  2

enum {
    FDR_SUCCESS             = 0,
    FDR_ERR_GENFAILURE
};

typedef struct CommandResult {
    std::string cmdOutput;
    int cmdExitstatus;
} CommandResult_t;

CommandResult_t exec(const char *cmd);
std::vector<std::string> split(std::string str, char delimter);

void FindAndReplaceFirst(std::string &s, const std::string &search, const std::string &replace);
void FindAndReplaceAll(std::string &s, const std::string &search, const std::string &replace);

extern std::string bootCounter;
extern std::string sensorDirTimestamp;
extern std::string fdrHmcAlivePathName; 
extern std::string CommonFdrKeepersDirName;

// some of the short and inlined function definition in header file
// [so that these functions are guranteed to be inlined by the compiler]
// Returns true if x is in range [low..high], else false   
inline bool CheckIsInRange(uint64_t low, uint64_t high, uint64_t x)
{
	return (low <= x && x <= high);
}

inline const char *strna(const char *s)
{
	return s ? s : "n/a";
}

inline std::string GetDirectoryName(void)
{
	std::string directoryName = "BootCount_" + bootCounter + "_DateStamp_" + sensorDirTimestamp;
	return directoryName;
}
