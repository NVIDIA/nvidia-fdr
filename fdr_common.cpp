/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include <array>
#include <chrono>
#include <cmath>
#include "fdr_log.hpp"
#include "fdr_common.hpp"

CommandResult_t exec(const char *cmd)
{
	int exitcode = 0;
	std::array<char, ONE_MB> buffer{};
	std::string result;

	FILE *pipe = popen(cmd, "r");
	if (pipe == nullptr)
	{
        // it can be called before fdr init
		fdrlog::warn("exec cmd failed: {}", cmd);
		throw std::runtime_error("popen() failed!");
	}
	try
	{
		std::size_t bytesread;
		while ((bytesread = std::fread(buffer.data(), sizeof(buffer.at(0)), sizeof(buffer), pipe)) != 0)
		{
			result += std::string(buffer.data(), bytesread);
		}
	}
	catch (...)
	{
		pclose(pipe);
		throw;
	}
	exitcode = WEXITSTATUS(pclose(pipe));

	return CommandResult_t{result, exitcode};
}

std::vector<std::string> split(std::string str, char delimter)
{
	std::vector<std::string> retSplitVector;
	// declaring temp string to store the curr "word" upto del
	std::string temp = "";

	for(int i=0; i<(int)str.size(); i++) {
		// If cur char is not del, then append it to the cur "word", otherwise
		// you have completed the word, print it, and start a new word.
		if(str[i] != delimter) {
			temp += str[i];
		} else {
			retSplitVector.push_back(temp);
			temp = "";
		}
	}

	retSplitVector.push_back(temp);

	return retSplitVector;
}

void FindAndReplaceFist(std::string &s, const std::string &search, const std::string &replace)
{
    std::size_t pos = s.find(search);
    if (pos == std::string::npos)
        return;
    s.replace(pos, search.length(), replace);
}

void FindAndReplaceAll(std::string &s, const std::string &search, const std::string &replace)
{
    std::size_t pos = s.find(search);
    while (pos != std::string::npos)
    {
        s.replace(pos, search.size(), replace);
        pos = s.find(search, pos + replace.size());
    }
}

LeakyBucket::LeakyBucket(int64_t capacity, double rate) : capacity{capacity}, rate{rate}, e{std::chrono::steady_clock::now()} {}

int64_t LeakyBucket::Capacity()
{
    return this->capacity;
}

float LeakyBucket::Rate()
{
    return this->rate;
}

int64_t LeakyBucket::Count()
{
     if (std::chrono::steady_clock::now() >= this->e)
    {
        return 0;
    }

    auto remaining_us = std::chrono::duration_cast<std::chrono::microseconds> (
            this->e - std::chrono::steady_clock::now()
        ).count();
    
    auto per_drip_us =  std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::seconds(1)).count() / this->rate;

    auto count = int64_t(ceil(double(remaining_us) / double(per_drip_us)));

    return count;
}

bool LeakyBucket::IsFull()
{
    return this->Count() >= this->capacity;
}

int64_t LeakyBucket::Add(int64_t amount)
{
    auto count = this->Count();
    if (count >= this->capacity)
    {
        // The bucket is full.
        return 0;
    }

    if (std::chrono::steady_clock::now() >= this->e)
    {
        // reset the bucket
        this->e = std::chrono::steady_clock::now();
    }

    auto remaining = this->capacity - count;
    if (amount > remaining)
    {
        amount = remaining;
    }

    auto duration_us = int64_t(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::seconds(1)).count() 
        / this->rate * amount);


    this->e += std::chrono::microseconds(duration_us);

    return amount;
}