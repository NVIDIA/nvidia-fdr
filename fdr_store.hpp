#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <sqlite3.h>
#include "fdr_logs_schema.pb.h"
//using namespace fdr;

const std::string ENCODING_CHOICE_JSON = "JSON";
const std::string ENCODING_CHOICE_BINARY = "BINARY";
const std::string ENCODING_CHOICE_DB = "DB";


struct fdr_sample_sql {
    uint64_t timestamp;     // Time at which this sample was captured
	unsigned int paramID;	// Unique ID to be used instead of ParamName when needed
    std::string paramType;   // Type of this sample Uint64|String|Binary etc.
    uint64_t paramValueInt64;        // Applicable when paramType==Uint64
    std::string paramValueString;    // Applicable when paramType==string
};

struct fdr_stat_sql {
	unsigned int paramID;	// Unique ID to be used instead of ParamName when needed
    uint64_t fromtime;   // Start of time period within which samples lie
    uint64_t totime;     // End of time period within which samples lie
    uint64_t numsamples; // Number of samples this stat took into consideration
    uint64_t min;        // Minimum value seen across all samples
    uint64_t max;        // Maximum value seen across all samples
    uint64_t avg;        // Average values for the sample set
};

class FDRStore
{
private:
    /* data */
    std::string storagefilepath;
    std::string encodingtouse;
    std::ifstream instream;
    std::ofstream outstream;

    std::string dbLoc;
    std::string paramClass, compClass;
    std::string compID;

    sqlite3 *DB;
    sqlite3_stmt* sampleFetchStmt;

    google::protobuf::io::ZeroCopyInputStream *binaryinzerocopystream;

public:
    FDRStore(std::string filename, std::string fileformat);
    FDRStore(std::string filename, std::string fileformat, std::string paramClass, std::string compClass, std::string compID);
    ~FDRStore();

    //void append(const google::protobuf::Message &data); // append data to file
    void append(const fdr_sample_sql &data);
    void append(const google::protobuf::Message &data);

    int readnext(fdr_sample_sql *datap); // read data at current pointer in file and advance pointer to next
    int readnext(google::protobuf::Message *datap);

    //int getLatest(fdr_sample *datap, std::string infoID);   // Get the latest record corresponding to the given infoID

    void deleteRecords();   // Delete all records in the table

    /* The following two functions can probably be shifted into the normal append/initialize functions */

    void createStatesTable();   //Creates the stats table if it does not exist

    void appendStat(const fdr_stat_sql &data);

    void rewind(); // reset pointer in file to begining of file
};
