/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

// This file contain definitions for all the methods related to Book of errors in FDR
#include "fdr.hpp"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <regex>

bool FlightDataRecorder_c::CompareMessageWithLog(const fdrpb::fdr_book_of_errors& errMssg, const std::string& logFile)
{

	if (!(std::filesystem::exists(logFile))) {
		log->warn("Book of errors file Not Exist!!: {}", logFile);
		return false;
	}
    else
    {
        fdrpb::fdr_book_of_errors errorBOERecord;
        FDRStore BookOfErrorReader(logFile, profile.GeneralConfig.LogsFormat, STORE_READER);
        while (BookOfErrorReader.readnext(&errorBOERecord)) 
        {
            // std::cout << "In book of errors INSIDE BOE READER WHILE" << std::endl;
            
            // std::cout << "From log file boot id" << errorBOERecord.bootid() << "From error message boot id" << errMssg.bootid() <<std::endl;
            // std::cout << "From log file comp class" << errorBOERecord.compclass() << "From error message comp class" << errMssg.compclass() <<std::endl;
            // std::cout << "From log file device instance" << errorBOERecord.deviceinstance() << "From error message device instance" << errMssg.deviceinstance() <<std::endl;
            // std::cout << "From log file param class" << errorBOERecord.paramclass() << "From error message param class" << errMssg.paramclass() <<std::endl;
            // std::cout << "From log file param id" << std::to_string(errorBOERecord.paramid()) << "From error message param id" << std::to_string(errMssg.paramid()) <<std::endl;
            // std::cout << "From log file error type" << errorBOERecord.errortype() << "From error message error type" << errMssg.errortype() <<std::endl;

            if (errMssg.bootid() == errorBOERecord.bootid() && 
                errMssg.compclass() == errorBOERecord.compclass() && 
                errMssg.deviceinstance() == errorBOERecord.deviceinstance() && 
                errMssg.paramclass() == errorBOERecord.paramclass() && 
                std::to_string(errMssg.paramid()) == std::to_string(errorBOERecord.paramid()) && 
                errMssg.errortype() == errorBOERecord.errortype()){
                    return true; }
        }
        return false;
    }

}

void FlightDataRecorder_c::SetBookOfErrorsRecord(unsigned int paramID, std::string sectionID, std::string componentID, std::string paramClass,
                                                 const char *value, time_t current_time, std::string bookOfErrorsFileName)
{
    book_of_errors.set_bootid(bootCounter);
    book_of_errors.set_paramid(paramID);
    book_of_errors.set_compclass(sectionID);
    book_of_errors.set_deviceinstance(componentID);
    book_of_errors.set_errortype(value);
    book_of_errors.set_erroroccurtimestamp(current_time); 
    book_of_errors.set_paramclass(paramClass);

    bool result = CompareMessageWithLog(book_of_errors, bookOfErrorsFileName);

    if (book_of_errors.ByteSizeLong() > 0 && !result )
    {
        fdrbookoferrorswriter->append(book_of_errors);
    }
}

void FlightDataRecorder_c::BookOfErrorEngine(std::string infoID, unsigned int paramID, std::string sectionID, std::string componentID, std::string paramClass,
                                             time_t current_time, PropertyVariant val)
{

    std::string bookOfErrorsFileName = profile.GeneralConfig.LogsBasePath + "/" + 
									   CommonFdrKeepersDirName + "/" + 
									   fdr->BookOfErrorKeeperName;

    // Check for Error Counters
    std::array<std::string, 6> errorsList1 = { "PCI-ERR-CTR-FATAL", "PCI-ERR-CTR-NON-FATAL", "PCI-ERR-CTR-UNSUPP-REQ", 
            "PCI-ERR-CTR-FATAL", "PCI-ERR-CTR-NON-FATAL", "PCI-ERR-CTR-UNSUPP-REQ" };

    std::array<std::string, 2> errorsList2 = { "NVLINK-ERR-CTR-RECOVERY" , "NVLINK-ERR-CTR-RECOVERY"};

    std::array<std::string, 2> healthFields = { "HEALTH" , "HEALTH-ROLLUP"};
    std::array<std::string, 2> healthErrorConditions = {  "Warning", "Critical"};

    // Checking for Error Counters
    if (std::find(std::begin(errorsList1), std::end(errorsList1), infoID) != std::end(errorsList1))
    {
        int64_t *err1_int64 = NULL; uint64_t *err1_uint64 = NULL; int32_t *err1_int32 = NULL; uint32_t *err1_uint32 = NULL; int16_t *err1_int16 = NULL; uint16_t *err1_uint16 = NULL;

        err1_int64 = std::get_if<int64_t>(&val);
        err1_uint64 = std::get_if<uint64_t>(&val);
        err1_uint32 = std::get_if<uint32_t>(&val);
        err1_int32 = std::get_if<int32_t>(&val);
        err1_int16 = std::get_if<int16_t>(&val);
        err1_uint16 = std::get_if<uint16_t>(&val);

        if (err1_int64 != nullptr) {
            if (*err1_int64 <=0){
                SetBookOfErrorsRecord(paramID, sectionID, componentID, paramClass, "Error Counter", current_time, bookOfErrorsFileName);
            }
        }
        if (err1_uint64 != nullptr) {
            if (*err1_uint64 <=0){
                SetBookOfErrorsRecord(paramID, sectionID, componentID, paramClass, "Error Counter", current_time, bookOfErrorsFileName);
            }
        }
        if (err1_uint32 != nullptr) {
            if (*err1_uint32 <=0){
                SetBookOfErrorsRecord(paramID, sectionID, componentID, paramClass, "Error Counter", current_time, bookOfErrorsFileName);
            }
        }
        if (err1_int32 != nullptr) {
            if (*err1_int32 <=0){
                SetBookOfErrorsRecord(paramID, sectionID, componentID, paramClass, "Error Counter", current_time, bookOfErrorsFileName);
            }
        }
        if (err1_uint16 != nullptr) {
            if (*err1_uint16 <=0){
                SetBookOfErrorsRecord(paramID, sectionID, componentID, paramClass, "Error Counter", current_time, bookOfErrorsFileName);
            }
        }
        if (err1_int16 != nullptr) {
            if (*err1_int16 <=0){
                SetBookOfErrorsRecord(paramID, sectionID, componentID, paramClass, "Error Counter", current_time, bookOfErrorsFileName);
            }
        }
        
    }

    else if (std::find(std::begin(errorsList2), std::end(errorsList2), infoID) != std::end(errorsList2))
    {
        int64_t *ptr_int64 = NULL; uint64_t *ptr_uint64 = NULL; int32_t *ptr_int32 = NULL; uint32_t *ptr_uint32 = NULL; int16_t *ptr_int16 = NULL; uint16_t *ptr_uint16 = NULL;
        ptr_int64 = std::get_if<int64_t>(&val);
        ptr_uint64 = std::get_if<uint64_t>(&val);
        ptr_uint32 = std::get_if<uint32_t>(&val);
        ptr_int32 = std::get_if<int32_t>(&val);
        ptr_int16 = std::get_if<int16_t>(&val);
        ptr_uint16 = std::get_if<uint16_t>(&val);

        if (ptr_int64 != nullptr) {
            if (*ptr_int64 <=2){
                SetBookOfErrorsRecord(paramID, sectionID, componentID, paramClass, "Error Counter", current_time, bookOfErrorsFileName);
            }
        }
        if (ptr_uint64 != nullptr) {
            if (*ptr_uint64 <=2){
                SetBookOfErrorsRecord(paramID, sectionID, componentID, paramClass, "Error Counter", current_time, bookOfErrorsFileName);
            }
        }
        if (ptr_uint32 != nullptr) {
            if (*ptr_uint32 <=2){
                SetBookOfErrorsRecord(paramID, sectionID, componentID, paramClass, "Error Counter", current_time, bookOfErrorsFileName);
            }
        }
        if (ptr_int32 != nullptr) {
            if (*ptr_int32 <=2){
                SetBookOfErrorsRecord(paramID, sectionID, componentID, paramClass, "Error Counter", current_time, bookOfErrorsFileName);
            }
        }
        if (ptr_int16 != nullptr) {
            if (*ptr_int16 <=2){
                SetBookOfErrorsRecord(paramID, sectionID, componentID, paramClass, "Error Counter", current_time, bookOfErrorsFileName);
            }
        }
        if (ptr_uint16 != nullptr) {
            if (*ptr_uint16 <=2){
                SetBookOfErrorsRecord(paramID, sectionID, componentID, paramClass, "Error Counter", current_time, bookOfErrorsFileName);
            }
        }
    }

    // Code to check NVSWITCH-PCI-ERR-CTR-CORR
    // Code to check GPU-ROW-REMAP-FAILED


    // Overall Health of a component
    else if (std::find(std::begin(healthFields), std::end(healthFields), infoID) != std::end(healthFields))
    {
        if (auto ptr (std::get_if<std::string>(&val)); ptr) {
            std::string str1 = *ptr;
            std::size_t found_critical = str1.find("Critical");
            std::size_t found_warning = str1.find("Warning");

            if (found_critical!=std::string::npos or found_warning!=std::string::npos)
            {
                SetBookOfErrorsRecord(paramID, sectionID, componentID, paramClass, "Health Related Fault", current_time, bookOfErrorsFileName);
            }

        }
    }
}
