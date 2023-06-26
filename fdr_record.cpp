/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include <iostream>
#include <fstream>
#include <filesystem>
#include <boost/algorithm/string.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/exception.hpp>
#include "fdr.hpp"
#include "fdr_common.hpp"


Record::Record(Profile_t &profile, Section_t &section,
              Component_t &component, std::shared_ptr<FDRStore> &fdrStoreObj,
              InfoGroup_t &infogroup, Info_t &info) : 
              profile(profile), section(section), component(component), 
              fdrreaderwriter(fdrStoreObj), infogroup(infogroup), info(info)
              
{   
    LastFetchedAt = 0; // Init last read time to epoch
    LastStoredAt = 0;  // Init last store time to epoch

    // Search & replace all params with values in the commands/paths
    for (auto &param : component.Params)
    {
        //$param.name --> param.value
        if (info.FetchMethod == "Command")
        {
            FindAndReplaceAll(info.CommandParams.Command, "$" + param.name, param.value);
        }
        else if (info.FetchMethod == "DBUS")
        {
            FindAndReplaceAll(info.DbusParams.Service, "$" + param.name, param.value);
            FindAndReplaceAll(info.DbusParams.ObjectPath, "$" + param.name, param.value);
            FindAndReplaceAll(info.DbusParams.Interface, "$" + param.name, param.value);
            FindAndReplaceAll(info.DbusParams.Property, "$" + param.name, param.value);
        }
    }

    logsformat = profile.GeneralConfig.LogsFormat;

    // for debugging purpose
    // Print();
}

Record::~Record()
{
    // std::cout << "Record Destructor called: " << section.ID 
    //           << "/" << component.ID 
    //           << "/" << infogroup.ID
    //           << "/" << info.ID 
    //           << "; fdrreaderwriter.use_count: " << fdrreaderwriter.use_count()
    //           << std::endl;
    // Print();
}

void Record::Refresh(void)
{
    // Skip if too early to refresh
    if (difftime(std::time(nullptr), LastFetchedAt) < info.FetchFreqSecs)
        return;

    std::time_t current_time = std::time(nullptr);
    data.fdr_sample_data.set_timestamp(current_time);
    //data.set_paramname(info.ID);
    data.paramtype = info.DataType;
    data.fdr_sample_data.set_paramid(info.ParamID);
    LastFetchedAt = current_time;

    if (info.FetchMethod == "Command")
    {
        CommandResult_t cmdResult = exec(info.CommandParams.Command.c_str());
		if (cmdResult.cmdExitstatus == FDR_ERR_GENFAILURE) {
			std::cout << "command Failed: " << info.CommandParams.Command << std::endl;
			return;
		}

        std::string commandresult = cmdResult.cmdOutput;
        if (data.paramtype == "Uint64")
        {
            std::istringstream str2num(commandresult);
            uint64_t val;
            str2num >> val;
            data.fdr_sample_data.set_paramvalueint64(val);
        }
        else
        {
            data.fdr_sample_data.set_paramvaluestring(commandresult);
        }
    }
    else if (info.FetchMethod == "DBUS")
    {
        // std::cout << "DbusParams Are: " << std::endl
        //           << "\tService: " << info.DbusParams.Service.c_str() << std::endl
        //           << "\tObjectPath: " << info.DbusParams.ObjectPath.c_str() << std::endl
        //           << "\tInterface: " << info.DbusParams.Interface.c_str() << std::endl
        //           << "\tProperty: " << info.DbusParams.Property.c_str() << std::endl;

        PropertyVariant val = dbus::readDbusProperty(info.DbusParams.Service, info.DbusParams.ObjectPath, 
                                                     info.DbusParams.Interface, info.DbusParams.Property);
        fdr->BookOfErrorEngine(info.ID, info.ParamID, section.ID, component.ID, info.parent_infogroup->ID ,current_time, val);

        // Sensors
        

        if (data.paramtype == "Uint64")
        {
            if (auto ptr (std::get_if<int64_t>(&val)); ptr)
            {
                // printf("int64 = %ld\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<uint32_t>(&val)); ptr)
            {
                // printf("uint32 = %u\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<uint64_t>(&val)); ptr)
            {
                // printf("uint64 = %lu\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<uint16_t>(&val)); ptr)
            {
                // printf("uint16 = %u\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<int16_t>(&val)); ptr)
            {
                // printf("int16 = %d\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<double>(&val)); ptr)
            {
                // printf("double = %lf\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<bool>(&val)); ptr)
            {
                // printf("bool = %d\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr = (std::get_if<std::tuple<bool, unsigned int>>(&val)); ptr)
            {
                // std::cout << "Successfully parsed: " << info.DbusParams.Property.c_str() << std::endl;
                const unsigned int intVal = std::get<1>(*ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) intVal);
                // std::cout << intVal << std::endl;
            }
            else {
                std::cout << "DBus read failed: Unknown numerical variant type: " 
                          << "; ObjectPath: " << info.DbusParams.ObjectPath
                          << "; Property: " << info.DbusParams.Property
                          << std::endl;
            }

        }
        else
        {
            if (auto ptr (std::get_if<std::string>(&val)); ptr) 
            {
                // printf("val =%s\n", ptr->c_str());
                data.fdr_sample_data.set_paramvaluestring(*ptr);
            }
        }
    } else if (info.FetchMethod == "Redfish") {
        if (!fdr->rfc) {
            std::cout << "Redfish not configured or not connected, skipping" << std::endl;
            return;
        }
        std::string uri = info.RedfishParams.URI;
        std::string json_pointer = info.RedfishParams.JSONPointer;
        std::cout << "RedfishParams URI: " << uri << " JSONPointer: " << json_pointer << std::endl;

        try {
            if (data.paramtype == "Uint64") {
                uint64_t u = fdr->rfc->query_uint64t (uri, json_pointer);
                data.fdr_sample_data.set_paramvalueint64(u);
            } else {
                // string
                std::string s = fdr->rfc->query_string (uri, json_pointer);
                data.fdr_sample_data.set_paramvaluestring(s);
            }
        } catch (const std::exception &e) {
            std::cerr << "Error fetching redfish: " << e.what() << std::endl;
        }
    }
    
    else if(info.FetchMethod == "DBUS_DGD")
    {
        // std::cout << "DbusDGDParams Are: " << std::endl
        //           << "\tParamID: " << info.ID.c_str() << std::endl
        //           << "\tService: " << info.DbusParams.Service.c_str() << std::endl
        //           << "\tObjectPath: " << info.DbusParams.ObjectPath.c_str() << std::endl
        //           << "\tInterface: " << info.DbusParams.Interface.c_str() << std::endl
        //           << "\tProperty: " << info.DbusParams.Property.c_str() << std::endl
        //           << "\tDevId: " << info.DbusParams.DevId << std::endl;

        RetCoreApi val = dbus::readDbusDGDProperty(info.DbusParams.Service, info.DbusParams.ObjectPath, 
                                                     info.DbusParams.Interface, info.DbusParams.Property, info.DbusParams.DevId);
        // std::cout << "Value of dbus device get property fields: " << std::get<2>(val) << std::endl;        
        data.fdr_sample_data.set_paramvalueint64(((uint64_t) std::get<2>(val))); 
    }

    else if(info.FetchMethod == "DBUS_PT")
    {
        // std::cout << "DbusPTParams Are: " << std::endl
        //           << "\tParamID: " << info.ID.c_str() << std::endl
        //           << "\tService: " << info.DbusParams.Service.c_str() << std::endl
        //           << "\tObjectPath: " << info.DbusParams.ObjectPath.c_str() << std::endl
        //           << "\tInterface: " << info.DbusParams.Interface.c_str() << std::endl
        //           << "\tFetchFreqSecs: " << info.FetchFreqSecs << std::endl
        //           << "\tOpcode: " << info.DbusParams.Opcode << std::endl 
        //           << "\tArg1: " << info.DbusParams.Arg1 << std::endl 
        //           << "\tArg2: " << info.DbusParams.Arg2 << std::endl;

        
        PassthroughFPGA fpga = dbus::readDbusPTProperty(info.DbusParams.Service, info.DbusParams.ObjectPath, 
                                                     info.DbusParams.Interface, info.DbusParams.Opcode, 
                                                     info.DbusParams.Arg1, info.DbusParams.Arg1);

        // std::cout << "Value of dbus fpga passthrough fields: " << std::get<1>(fpga) << std::endl;   
        data.fdr_sample_data.set_paramvalueint64(((uint64_t) std::get<1>(fpga))); 
     
    }
}

bool same_data_values(const fdr_sample_ext &left, const fdr_sample_ext &right)
{
    if ((left.fdr_sample_data.paramid() != right.fdr_sample_data.paramid()) || (left.paramtype != right.paramtype))
        return false;

    if (left.paramtype == "Uint64"){
        return (left.fdr_sample_data.paramvalueint64() == right.fdr_sample_data.paramvalueint64());
    }
    else{
        return (left.fdr_sample_data.paramvaluestring() == right.fdr_sample_data.paramvaluestring());
    }

    /*switch (left.paramType)
    {
    case InfoType::UINT64:
        return (left.paramValue.paramValueInt64 == right.paramValue.paramValueInt64);
        break;
    case InfoType::STRING:
        return (left.paramValue.paramValueString == right.paramValue.paramValueString);
        break;
    case 2:
        return true;
        break;
    default:
        return true;
    }*/
}

void print_data(const std::string name, const fdr_sample_ext &dat)
{

    std::cout << "Name: " + name << std::endl;
    std::cout << "dat.paramID: "  << dat.fdr_sample_data.paramid() << std::endl;
    std::cout << "paramtype: " << dat.paramtype << std::endl;
    if (dat.paramtype == "Uint64")
        std::cout << "dat.paramValueint64: " << dat.fdr_sample_data.paramvalueint64() << std::endl;
    else
        std::cout << "dat.paramValuestring: " << dat.fdr_sample_data.paramvaluestring() << std::endl;
}

void Record::Store(void)
{   
    // Skip if update not necessary per the policy
    if ((info.StorePolicy == "OnChange") && (same_data_values(data, last_stored_data)))
    {
        return;
    }

    // Skip if its not time to store yet
    if ((info.StorePolicy == "Periodic") && (difftime(std::time(nullptr), LastStoredAt) < info.StoreFreqSecs))
    {
        return;
    }

    // Skip if last store is quite older than last fetch
    if ((info.StorePolicy == "EveryFetch") && (difftime(LastFetchedAt, LastStoredAt) < info.FetchFreqSecs))
    {
        return;
    }

    // if (data.paramName() == "Model")
    // {
    //     std::cout << "Writing Model for ID " << info.parent_infogroup->parent_component->ID << std::endl;
    //     print_data("last_stored_data", last_stored_data);
    //     print_data("data", data);
    // }

    if (logsformat == ENCODING_CHOICE_JSON || logsformat == ENCODING_CHOICE_BINARY){
        fdrreaderwriter->append(data.fdr_sample_data);
        // Write to book of errors only when there is an error event
        // fdr->CheckForErrorsToUpdateBookOfErrors();
    }

    else if (logsformat == ENCODING_CHOICE_DB){
        fdr_sample_sql sqlDat;
        sqlDat.timestamp = data.fdr_sample_data.timestamp();
        sqlDat.paramID = data.fdr_sample_data.paramid();
        sqlDat.paramType = data.paramtype;
        if (data.paramtype == "Uint64"){
            sqlDat.paramValueInt64 = data.fdr_sample_data.paramvalueint64();
        }
        else{
            sqlDat.paramValueString = data.fdr_sample_data.paramvaluestring();
        }
        fdrreaderwriter->append(sqlDat);
    }

    // if (info.ID == "Model")
    //     std::cout << "Setting last_stored_data = data for ID " << info.parent_infogroup->parent_component->ID << std::endl;
    last_stored_data = data;
    LastStoredAt = std::time(nullptr);
}

// Read the last record of our type from the storage
void Record::Load(void)
{
    if (logsformat == ENCODING_CHOICE_JSON || logsformat == ENCODING_CHOICE_BINARY){
        fdrpb::fdr_sample readrec;
        while (fdrreaderwriter->readnext(&readrec))
        { // TODO: read the file from last to first
            if (readrec.paramid() == info.ParamID)
            {
                data.fdr_sample_data = last_stored_data.fdr_sample_data = readrec;
            }
        }
    }
    else if (logsformat == ENCODING_CHOICE_DB){
        fdr_sample_sql readrec;
        while (fdrreaderwriter->readnext(&readrec))
        { // TODO: read the file from last to first
            if (readrec.paramID == info.ParamID)
            {
                data.fdr_sample_data.set_timestamp(readrec.timestamp);
                data.fdr_sample_data.set_paramid(readrec.paramID);
                if (data.paramtype == "Uint64"){
                    data.fdr_sample_data.set_paramvalueint64(readrec.paramValueInt64);
                }
                else{
                    data.fdr_sample_data.set_paramvaluestring(readrec.paramValueString);
                }
                last_stored_data = data;
            }
        }
    }

    // last_stored_data.paramValue = 0;
}

void Record::Print(void)
{
    //std::cout << "Set last_stored_data.paramValue=" + last_stored_data.paramValueString << std::endl;
    std::cout << "---------------------------------------" << std::endl;
    std::cout << "this: " << this << std::endl;
    std::cout << "GeneralConfig.LogsFormat: " << profile.GeneralConfig.LogsFormat << std::endl
              << "\tGeneralConfig.LogsBasePath: " << profile.GeneralConfig.LogsBasePath << std::endl
              << "\tGeneralConfig.CompactionWindowSecs: " << profile.GeneralConfig.CompactionWindowSecs << std::endl;
    std::cout << "section.ID: " << section.ID << std::endl
              << "\tComponent.ID: " << component.ID << std::endl
              << "\t\tinfogroup.ID: " << infogroup.ID << std::endl
              << "\t\t\tRecordRetentionPolicy: " << infogroup.RecordRetentionPolicy << std::endl
              << "\t\t\tCompactionMethod: " << infogroup.CompactionMethod << std::endl
              << "\t\t\tCompactionFreqSecs: " << infogroup.CompactionFreqSecs << std::endl
              << "\t\t\tLastCompactedAt: " << infogroup.LastCompactedAt << std::endl
              << "\t\t\tRecordRetentionPolicy: " << infogroup.RecordRetentionPolicy << std::endl
              << "\t\t\tinfo.ID: " << info.ID << std::endl
              << "\t\t\t\tFetchPolicy: " << info.FetchPolicy << std::endl
              << "\t\t\t\tFetchMethod: " << info.FetchMethod << std::endl
              << "\t\t\t\tStorePolicy: " << info.StorePolicy << std::endl
              << "\t\t\t\tFetchFreqSecs: " << info.FetchFreqSecs << std::endl
              << "\t\t\t\tStoreFreqSecs: " << info.StoreFreqSecs << std::endl
              << "\t\t\t\tDataType: " << info.DataType << std::endl
              << "\t\t\t\tCommandParams.command: " << info.CommandParams.Command << std::endl
              << "\t\t\t\tCommandParams.WorkingDir: " << info.CommandParams.WorkingDir << std::endl
              << "\t\t\t\tDbusParams.Service: " << info.DbusParams.Service << std::endl
              << "\t\t\t\tDbusParams.ObjectPath: " << info.DbusParams.ObjectPath << std::endl
              << "\t\t\t\tDbusParams.Interface: " << info.DbusParams.Interface << std::endl
              << "\t\t\t\tDbusParams.Property: " << info.DbusParams.Property << std::endl
              << "\t\t\t\tDataType: " << info.DataType << std::endl;

    std::cout << "logsformat: " << logsformat << std::endl;
    std::cout << "fdrreaderwriter: " << fdrreaderwriter.get() << std::endl;
}
