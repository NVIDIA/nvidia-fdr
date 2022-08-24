#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <ctime>
#include <variant>

#include "fdr_policy.hpp"
#include "fdr_store.hpp"

class Record
{
private:
    Profile_t &profile;     // Ref to the policy
    Section_t &section;     // Ref to the section this record belongs to
    Component_t &component; // Ref to the component this record belongs to
    InfoGroup_t &infogroup; // Ref to the infogroup this record belongs to

    std::time_t LastFetchedAt; // Time of last fetch
    std::time_t LastStoredAt;  // Time of last store

    std::string logdir;      // base directory for logs
    std::string logfile;     // relative filename of logs
    std::string logfilepath; // full filepath of logs
    std::string logsformat;  // encoding format for logfiles

    std::unique_ptr<FDRStore> fdrreaderwriter;

public:
    Info_t &info; // How to create/update this record

    fdr::fdr_sample data;             // Data that will actually land in the DB
    fdr::fdr_sample last_stored_data; // Last fetched value. Used to determine if anything changed

    Record(Profile_t &profile, Section_t &section, Component_t &component, InfoGroup_t &infogroup, Info_t &info);

    ~Record(){};

    void Refresh(void); // Update the record with fresh info from platform
    void Load(void);    // Read the (last) record from logfile into the record
    void Store(void);   // Write the record out to the file

    void Print(void);
};
