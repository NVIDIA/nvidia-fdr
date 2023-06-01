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

#include <stdlib.h>
#include <stdio.h>
#include <ctime>
#include <variant>

#include "fdr_logs_schema.pb.h"
#include "fdr_policy.hpp"
#include "fdr_store.hpp"

struct fdr_sample_ext { // Extended version of the fdr_sample proto message
    fdrpb::fdr_sample fdr_sample_data;
    std::string paramtype;
    std::string paramname;
};

class Record
{
private:
    Profile_t &profile;     // Ref to the policy
    Section_t &section;     // Ref to the section this record belongs to
    Component_t &component; // Ref to the component this record belongs to

    std::time_t LastFetchedAt; // Time of last fetch
    std::time_t LastStoredAt;  // Time of last store

    std::string logsformat;  // encoding format for logfiles

    std::shared_ptr<FDRStore> fdrreaderwriter;

public:
    InfoGroup_t &infogroup; // Ref to the infogroup this record belongs to
    Info_t &info; // How to create/update this record

    fdr_sample_ext data;             // Data that will actually land in the DB
    fdr_sample_ext last_stored_data; // Last fetched value. Used to determine if anything changed

    Record(Profile_t &profile, Section_t &section,
           Component_t &component, std::shared_ptr<FDRStore> &fdrLogWriter,
           InfoGroup_t &infogroup, Info_t &info);

    ~Record();

    void Refresh(void); // Update the record with fresh info from platform
    void Load(void);    // Read the (last) record from logfile into the record
    void Store(void);   // Write the record out to the file

    void Print(void);


};
