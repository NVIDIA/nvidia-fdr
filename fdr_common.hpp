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

#include <chrono>
#include <ostream>
#include <string>
#include <stdlib.h>
#include <vector>
#include <iostream>

#define ONE_MB              (1024UL*1024UL)
#define BOOT_TIME_DIR_COUNT 1
#define RUN_TIME_DIR_COUNT  2

enum {
    FDR_SUCCESS             = 0,
    FDR_ERR_GENFAILURE,
    FDR_ERR_PARTITION_SIZE_LESS,
    FDR_ERR_MAIN_WINDOW_NOT_EXPIRED,
    FDR_ERR_SUB_WINDOW_NOT_EXPIRED
};

typedef struct CommandResult {
    std::string cmdOutput;
    int cmdExitstatus;
} CommandResult_t;

CommandResult_t exec(const char *cmd);
std::vector<std::string> split(std::string str, char delimter);

void FindAndReplaceFirst(std::string &s, const std::string &search, const std::string &replace);
void FindAndReplaceAll(std::string &s, const std::string &search, const std::string &replace);


class LeakyBucket {
    private:
        // capacity of the bucket
        int64_t capacity;
        // leaking rate or process rate of the bucket, per second
        // e.g. 0.1 means it take 10 seconds to process a request.
        double rate;
        // e is the exact time the bucket will have leaked enough to be empty
        std::chrono::time_point<std::chrono::steady_clock> e;
    public:

        LeakyBucket(int64_t capacity, double rate);

        int64_t Capacity();
        float Rate();

        int64_t Count();
        bool IsFull();

        // Add 'amount' to the bucket's up to the capacity, and return the actual added amount.
        // If the the return value is smaller then 'amount', the bucket is full.
        int64_t Add(int64_t amount);
};

extern std::string bootCounter;
extern std::string sensorDirTimestamp;
extern std::string fdrHmcAlivePathName; 
extern std::string CommonFdrKeepersDirName;
extern uint64_t numFdsObjs;

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
