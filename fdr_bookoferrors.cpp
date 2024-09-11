/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

// This file contain definitions for all the methods related to Book of errors
// in FDR
#include "fdr.hpp"
#include "fdr_log.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>

bool FlightDataRecorder_c::CompareMessageWithLog(
    const fdrpb::fdr_book_of_errors& errMssg)
{
    std::string bookOfErrorsFileName = profile.GeneralConfig.LogsBasePath +
                                       "/" + CommonFdrKeepersDirName + "/" +
                                       fdr->BookOfErrorKeeperName;

    if (!(std::filesystem::exists(bookOfErrorsFileName)))
    {
        fdrlog::warn("Book of errors file Not Exist!!: {}",
                     bookOfErrorsFileName);
        return false;
    }
    else
    {
        int fileReadRetVal;
        fdrpb::fdr_book_of_errors errorBOERecord;
        FDRStore BookOfErrorReader(bookOfErrorsFileName,
                                   profile.GeneralConfig.LogsFormat,
                                   STORE_READER);
        // 1. loop through the file
        while ((fileReadRetVal = BookOfErrorReader.readnext(&errorBOERecord)) ==
               FDR_SUCCESS_DATA_READ)
        {
            // std::cout << "In book of errors INSIDE BOE READER WHILE" <<
            // std::endl;

            // std::cout << "From log file boot id" << errorBOERecord.bootid()
            // << "From error message boot id" << errMssg.bootid() <<std::endl;
            // std::cout << "From log file device instance" <<
            // errorBOERecord.deviceinstance() << "From error message device
            // instance" << errMssg.deviceinstance() <<std::endl; std::cout <<
            // "From log file param id" <<
            // std::to_string(errorBOERecord.paramid()) << "From error message
            // param id" << std::to_string(errMssg.paramid()) <<std::endl;
            // std::cout << "From log file error type" <<
            // errorBOERecord.errortype() << "From error message error type" <<
            // errMssg.errortype() <<std::endl;

            if (errMssg.bootid() == errorBOERecord.bootid() &&
                errMssg.deviceinstance() == errorBOERecord.deviceinstance() &&
                errMssg.paramid() == errorBOERecord.paramid() &&
                errMssg.errortype() == errorBOERecord.errortype())
            {
                return true;
            }
        }
        // 2. check for corrupted file. If yes, then take appropriate action to
        // exit the function
        if (fileReadRetVal == FDR_ERR_DATA_READ_CORRUPT_EOF)
        {
            fdrlog::error(
                "CompareMessageWithLog: File corruption identified. Skip the functionality.");
            return false;
        }

        return false;
    }
}

void FlightDataRecorder_c::SetBookOfErrorsRecord(unsigned int paramID,
                                                 std::string componentID,
                                                 const char* value,
                                                 time_t current_time)
{
    book_of_errors.set_bootid(bootCounter);
    book_of_errors.set_paramid(paramID);
    book_of_errors.set_deviceinstance(componentID);
    book_of_errors.set_errortype(value);
    book_of_errors.set_erroroccurtimestamp(current_time);

    bool result = CompareMessageWithLog(book_of_errors);

    if (book_of_errors.ByteSizeLong() > 0 && !result)
    {
        fdrbookoferrorswriter->append(book_of_errors);
    }
}

void FlightDataRecorder_c::BookOfErrorEngine(std::string infoID,
                                             unsigned int paramID,
                                             std::string componentID,
                                             time_t current_time,
                                             PropertyVariant /*val*/)
{
    // Record Faults into book of errors
    if (infoID == "FAULTS")
    {
        SetBookOfErrorsRecord(paramID, componentID, "Faults and Errors",
                              current_time);
    }
    else
    {
        SetBookOfErrorsRecord(paramID, componentID, "Error Counter",
                              current_time);
    }
}
