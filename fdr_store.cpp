/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include <string>
#include <sstream>
#include "fdr_log.hpp"
#include "fdr_store.hpp"

#include "fdr_logs_schema.pb.h"
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/delimited_message_util.h>
#include <google/protobuf/io/zero_copy_stream.h>

// This global variable is just for debugging to track the number of FDRStore objects
// created for the whole FDR instance
uint64_t numFdsObjs=0;
FDRStore::FDRStore(std::string filename, std::string fileformat, int isStoreReaderWriter) : binaryinzerocopystream(nullptr)    //Protobuf/json version
{
    storagefilepath = filename;
    encodingtouse = fileformat;

    numFdsObjs++;
    // std::string readerOrWriterStr = (isStoreReaderWriter == STORE_READER) ? std::string("STORE_READER") : std::string("STORE_WRITER");
    // std::cout << "FDRStore[construtor]: numFdsObjs: " << numFdsObjs
    //           << "; storagefilepath: " << storagefilepath
    //           << "; " << readerOrWriterStr
    //           << std::endl;

    // open the instream only for the reader requester
    if (isStoreReaderWriter == STORE_READER) {
        instream.open(storagefilepath);
        if (encodingtouse == "BINARY") {
            binaryinzerocopystream = new google::protobuf::io::IstreamInputStream(&instream);
        }
    }
}

FDRStore::~FDRStore()
{
    numFdsObjs--;
    // std::cout << "FDRStore[destrutor]: numFdsObjs: " << numFdsObjs
    //           << "; storagefilepath: " << storagefilepath
    //           << std::endl;
    if (binaryinzerocopystream) {
        delete binaryinzerocopystream;
    }
    instream.close();

    // std::cout << "2. FDRStore Destructor called" << std::endl;
    // std::cout << "3. FDRStore Destructor called" << std::endl;
}

void FDRStore::append(const google::protobuf::Message &data)
{
    if (encodingtouse == ENCODING_CHOICE_JSON)
    {
        outstream.open(storagefilepath, std::ios_base::app);
        if (outstream.is_open())
        {
            std::string jsonstr;
            google::protobuf::util::MessageToJsonString(data, &jsonstr);
            outstream << jsonstr << std::endl;
            outstream.close();
        }
        else
        {
            // TODO: Error handling
        }
    }
    else if(encodingtouse == ENCODING_CHOICE_BINARY)
    {
        outstream.open(storagefilepath, std::ios::binary | std::ios::app);
        google::protobuf::util::SerializeDelimitedToOstream(data, &outstream);
        outstream.close();
    }
    else
    {
    	// Use the other function for SQLite
    }
}

int FDRStore::readnext(google::protobuf::Message *datap){
    // std::cout << "readnext:encodingtouse: " << encodingtouse << "; storagefilepath: " << storagefilepath << std::endl;
    if (encodingtouse == ENCODING_CHOICE_JSON)
    {
        if (instream.is_open())
        {
            std::string line;
            if (std::getline(instream, line))
            {
                google::protobuf::util::JsonStringToMessage(line, datap);
                return 1;
            }
        }
        return 0;
    }
    else if(encodingtouse == ENCODING_CHOICE_BINARY)
    {
        if (instream.is_open())
        {
            bool clean_eof = true;
            auto ret = google::protobuf::util::ParseDelimitedFromZeroCopyStream(datap, binaryinzerocopystream, &clean_eof);

            if (ret == false)
            {
                if (clean_eof)
                {
                    return 0; // clean end of file
                }
                else
                {
                    fdrlog::warn("Binary file seems corrupted");
                    return 0; // Unexpected end of file
                }
            }
            return 1; //Successfull read of a record and more left to read
        }
    }
    else{
        // Use the other function for SQLite
    }
    // std::cout << "readnext: default: failure: return 0: encodingtouse: " << encodingtouse
    //           << "; storagefilepath: " << storagefilepath 
    //           << std::endl;
    return 0;
}


std::string FDRStore::getStoreFilePath()
{
    return storagefilepath;
}