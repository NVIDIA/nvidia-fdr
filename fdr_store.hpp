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
#include <fstream>
#include <iostream>
#include "fdr_logs_schema.pb.h"
//using namespace fdr;

#define STORE_WRITER 0
#define STORE_READER 1

const std::string ENCODING_CHOICE_JSON = "JSON";
const std::string ENCODING_CHOICE_BINARY = "BINARY";


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

    google::protobuf::io::ZeroCopyInputStream *binaryinzerocopystream;

    //Helpful to debug if properties are not set 
    void printUnsetFields(const google::protobuf::Message &data);

public:
    FDRStore(std::string filename, std::string fileformat, int isStoreReaderWriter);
    FDRStore(std::string filename, std::string fileformat, std::string paramClass, std::string compClass, std::string compID);
    ~FDRStore();

    //void append(const google::protobuf::Message &data); // append data to file
    void append(const google::protobuf::Message &data);

    int readnext(google::protobuf::Message *datap);

    //int getLatest(fdr_sample *datap, std::string infoID);   // Get the latest record corresponding to the given infoID

    void deleteRecords();   // Delete all records in the table

    /* The following two functions can probably be shifted into the normal append/initialize functions */

    void createStatesTable();   //Creates the stats table if it does not exist

    void rewind(); // reset pointer in file to begining of file

    std::string getStoreFilePath(); // Returns storage file path
};
