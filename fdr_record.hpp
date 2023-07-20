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
#include <spdlog/spdlog.h>

#include "fdr_logs_schema.pb.h"
#include "fdr_policy.hpp"
#include "fdr_store.hpp"
#include "property_variant.hpp"

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

    std::shared_ptr<FDRStore> fdrLogReaderWriter;
    std::shared_ptr<FDRStore> fdrStatwriter;

    fdrpb::fdr_stat runningStatus;

public:
    InfoGroup_t &infogroup; // Ref to the infogroup this record belongs to
    Info_t &info; // How to create/update this record

    fdr_sample_ext data;             // Data that will actually land in the DB
    fdr_sample_ext last_stored_data; // Last fetched value. Used to determine if anything changed

    Record(Profile_t &profile, Section_t &section,
           Component_t &component, std::shared_ptr<FDRStore> &fdrLogWriter,  std::shared_ptr<FDRStore> &fdrStatWriter,
           InfoGroup_t &infogroup, Info_t &info);

    ~Record();

    void Refresh(bool viaTimerSkipChecks); // Update the record with fresh info from platform
    void Store(void);   // Write the record out to the file
    void RunningStatisticEngine(fdrpb::fdr_sample readrec);
    void appendRunningStatToStatfile(void);
    // reset all the variables related to stat
    inline void ResetRunningStat(void) {
        runningStatus.clear_fromtime();
        runningStatus.clear_totime();
        runningStatus.clear_numsamples();
        runningStatus.clear_min();
        runningStatus.clear_max();
        runningStatus.clear_avg();
        runningStatus.clear_minvaltimestamp();
        runningStatus.clear_maxvaltimestamp();
        runningStatus.clear_paramid();
    }

    inline std::string print_fdrStatwriterStoragefilepath(void) {
        return fdrStatwriter->getStoreFilePath();
    }

   	// release the existing store pointer[will call the store object's destructor]
    inline void ResetLogStatStorePtrs(void) {
        fdrLogReaderWriter.reset();
        fdrStatwriter.reset();
    }
    // all assign a new store object pointers
    inline void ResetLogStatStorePtrs(std::shared_ptr<FDRStore> &fdrNewLogWriter,
                                      std::shared_ptr<FDRStore> &fdrNewStatWriter) {
        fdrLogReaderWriter = fdrNewLogWriter;
        fdrStatwriter = fdrNewStatWriter;
    }
    inline std::string GetSectionID(void) {
        return section.ID;
    }
    inline std::string GetComponentID(void) {
        return component.ID;
    }
    inline std::string GetInfoGroupID(void) {
        return infogroup.ID;
    }
    inline std::string GetInfoListID(void) {
        return info.ID;
    }
    void Print(void);

    /* Callback to refresh the record on propertyChangedSignals message */
    void refreshDataCallback(PropertyVariant val);

};
