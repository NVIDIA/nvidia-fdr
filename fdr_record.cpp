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
#include <sdbusplus/bus.hpp>
#include <sdbusplus/exception.hpp>
#include "fdr_log.hpp"
#include "fdr.hpp"
#include "fdr_common.hpp"
#include "fdr_log.hpp"


Record::Record(Profile_t &profile, Section_t &section,
              Component_t &component, std::shared_ptr<FDRStore> &fdrStoreObj, std::shared_ptr<FDRStore> &fdrStatStoreObj,
              InfoGroup_t &infogroup, Info_t &info) : 
              profile(profile), section(section), component(component), 
              fdrLogReaderWriter(fdrStoreObj), fdrStatwriter(fdrStatStoreObj),
              infogroup(infogroup), info(info)
              
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
    
    // reset all the variables related to stat
    ResetRunningStat();

    // for debugging purpose
    // Print();
}

Record::~Record()
{
    // std::cout << "Record Destructor called: " << section.ID 
    //           << "/" << component.ID 
    //           << "/" << infogroup.ID
    //           << "/" << info.ID 
    //           << "; fdrLogReaderWriter.use_count: " << fdrLogReaderWriter.use_count()
    //           << std::endl;
    // Print();
}

void Record::Refresh(bool viaTimerSkipChecks)
{
    // Skip if too early to refresh
    if (viaTimerSkipChecks == false) {
        if (difftime(std::time(nullptr), LastFetchedAt) < info.FetchFreqSecs)
            return;
    }

    std::time_t current_time = std::time(nullptr);
    data.fdr_sample_data.set_timestamp(current_time);
    //data.set_paramname(info.ID);
    data.paramtype = info.DataType;
    data.fdr_sample_data.set_paramid(info.ParamID);
    LastFetchedAt = current_time;

    if (info.FetchMethod == "DBUS")
    {
        // std::cout << "DbusParams Are: " << std::endl
        //           << "\tService: " << info.DbusParams.Service.c_str() << std::endl
        //           << "\tObjectPath: " << info.DbusParams.ObjectPath.c_str() << std::endl
        //           << "\tInterface: " << info.DbusParams.Interface.c_str() << std::endl
        //           << "\tProperty: " << info.DbusParams.Property.c_str() << std::endl;

        PropertyVariant val = dbus::readDbusProperty(info.DbusParams.Service, info.DbusParams.ObjectPath, 
                                                     info.DbusParams.Interface, info.DbusParams.Property);
        fdr->BookOfErrorEngine(info.ID, info.ParamID, component.ID, current_time, val);

        // Sensors
        

        if (data.paramtype == "Uint64")
        {
            if (auto ptr (std::get_if<double>(&val)); ptr)
            {
                // printf("double = %lf\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<uint32_t>(&val)); ptr)
            {
                // printf("uint32 = %u\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<uint16_t>(&val)); ptr)
            {
                // printf("uint16 = %u\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<uint64_t>(&val)); ptr)
            {
                // printf("uint64 = %lu\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<int64_t>(&val)); ptr)
            {
                // printf("int64 = %ld\n", *ptr);
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
            else if (auto ptr (std::get_if<uint8_t>(&val)); ptr)
            {
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<int16_t>(&val)); ptr)
            {
                // printf("int16 = %d\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else {
                // fdrlog::warn("DBus read failed: Unknown numerical variant type: "
                //          "; ObjectPath: {}; Property: {}",
                //           info.DbusParams.ObjectPath,
                //           info.DbusParams.Property);
    			return;
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
    } else if (info.FetchMethod == "Command") {
        CommandResult_t cmdResult = exec(info.CommandParams.Command.c_str());
		if (cmdResult.cmdExitstatus != FDR_SUCCESS) {
            fdrlog::warn("command Failed: {}", info.CommandParams.Command);
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
    } else if (info.FetchMethod == "Redfish") {
        if (!fdr->rfc) {
            fdrlog::debug("Redfish not configured or not connected, skipping");
            return;
        }
        std::string uri = info.RedfishParams.URI;
        std::string json_pointer = info.RedfishParams.JSONPointer;
        fdrlog::debug("RedfishParams URI: {}, JSONPointer: {}", uri, json_pointer);

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
            fdrlog::warn("Error fetching redfish: {}", e.what());
            return;
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
    else
    {
    	return;
    }

    // [if it reaches here, then a definite data is available for consumption]
    // on every record execution, store or update the running stat variables which will 
    // used at the subwindow expiry time. 
    if (infogroup.CompactionMethod == "Average") {
        RunningStatisticEngine(data.fdr_sample_data);
    }

}

// this method will be called on every record execution.
// using the just acquired recent record values, store or update the running stat variables which will 
// written in the stat file on every subwindow expiry time.
void Record::RunningStatisticEngine(fdrpb::fdr_sample readrec)
{
    auto currentRecValue = readrec.paramvalueint64();
    auto currentRecTimestamp = readrec.timestamp();

    runningStatus.set_paramid(readrec.paramid());
    runningStatus.set_numsamples(runningStatus.numsamples() + 1);
    runningStatus.set_avg(runningStatus.avg() + currentRecValue); // TODO: using avg field as sum. avoid overflow.

    // set min value for the very first time
    if (runningStatus.min() == 0 && runningStatus.minvaltimestamp() == 0) {
        runningStatus.set_min(currentRecValue);
        runningStatus.set_minvaltimestamp(currentRecTimestamp);
    } else {
        int64_t min_value = std::min(runningStatus.min(), currentRecValue);
        runningStatus.set_min(min_value);
        if (min_value == currentRecValue) {
            // need to record the timestamp for min value
            runningStatus.set_minvaltimestamp(currentRecTimestamp);
        }
    }

    int64_t max_value = std::max(runningStatus.max(), currentRecValue);
    runningStatus.set_max(max_value);
    if (max_value == currentRecValue) {
        // need to record the timestamp for max value
        runningStatus.set_maxvaltimestamp(currentRecTimestamp);
    }
    runningStatus.set_fromtime(runningStatus.fromtime() == 0 ? currentRecTimestamp : runningStatus.fromtime());
    runningStatus.set_totime(currentRecTimestamp);

}

void Record::appendRunningStatToStatfile(void)
{
    if (runningStatus.numsamples() == 0) {
        // This print could be false alarm as well, as CompactionWindowSecs and CompactionSubWindowSecs could
        // finish at the same time and Compactor() could have already append the statistic data to the stat files
        // and would have cleared the runningStatus elements; hence, CompactionSubWindowSecs timerCB when tries
        // append the runningStatus data, it sees all are cleared and will return from here.
        // fdrlog::warn("{}: num sample is zero: skipping stat update!", infogroup.parent_component->ID);
        return;
    }
    runningStatus.set_avg(runningStatus.avg() / runningStatus.numsamples());
    // finally, append the stat record to the stat file
    fdrStatwriter->append(runningStatus);

    // for debugging
    // std::cout << "-----------component: " << infogroup.parent_component->ID
    //           << "; info.ParamID: " << info.ID
    //           << "; FromTime: " << runningStatus.fromtime()
    //           << "; totime: " << runningStatus.totime()
    //           << "; numsamples: " << runningStatus.numsamples()
    //           << "; min: " << runningStatus.min()
    //           << "; max: " << runningStatus.max()
    //           << "; avg: " << runningStatus.avg()
    //           << "; minvaltimestamp: " << runningStatus.minvaltimestamp()
    //           << "; maxvaltimestamp: " << runningStatus.maxvaltimestamp()
    //           << "; paramid: " << runningStatus.paramid()
    //           << "----------" << std::endl;
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
    if (info.StorePolicy == "OnChange") {
        if (same_data_values(data, last_stored_data)) {
            return;
        }
    }
    // Skip if last store is quite older than last fetch
    else if (info.StorePolicy == "EveryFetch") {
        if (difftime(LastFetchedAt, LastStoredAt) < info.FetchFreqSecs) {
            return;
        }
    }
    // Skip if its not time to store yet
    else if (info.StorePolicy == "Periodic") {
        if (difftime(std::time(nullptr), LastStoredAt) < info.StoreFreqSecs) {
            return;
        }
    }

    // For debugging : No debugging needed for poll records
    // if (info.FetchType == "Subscribe")
    // {
    //     fdrlog::debug("Store record for objectPath = {}, interface = {}, property = {}",
    //         info.DbusParams.ObjectPath, info.DbusParams.Interface, info.DbusParams.Property);
    // }

    fdrLogReaderWriter->append(data.fdr_sample_data);

    last_stored_data = data;
    LastStoredAt = std::time(nullptr);
}

void Record::refreshDataCallback(PropertyVariant val)
{
    // For errors counter run book of errors
    // Write to both fdr reader writer as well as book of errors

    fdrlog::debug("Refresh record data for objectPath = {}, interface = {}, property = {}",
        info.DbusParams.ObjectPath, info.DbusParams.Interface, info.DbusParams.Property);

    std::time_t current_time = std::time(nullptr);
    data.fdr_sample_data.set_timestamp(current_time);
    data.paramtype = info.DataType;
    data.fdr_sample_data.set_paramid(info.ParamID);

    if (infogroup.ID == "Error"){
        fdr->BookOfErrorEngine(info.ID, info.ParamID, component.ID, current_time, val);
    }

    if (auto ptr (std::get_if<std::string>(&val)); ptr){
        data.fdr_sample_data.set_paramvaluestring(*ptr);
    }
    else if (auto ptr (std::get_if<std::uint64_t>(&val)); ptr){
        data.fdr_sample_data.set_paramvalueint64(*ptr);
    }
    // Update LastFetchedAt timestamp
    this->LastFetchedAt = std::time(nullptr);
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
              << "\t\t\t\tFetchType: " << info.FetchType << std::endl
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
    std::cout << "fdrLogReaderWriter: " << fdrLogReaderWriter.get() << std::endl;
    std::cout << "fdrStatwriter: " << fdrStatwriter.get() << std::endl;
}
