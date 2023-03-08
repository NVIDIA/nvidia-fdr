#include <iostream>
#include <fstream>
#include "fdr_common.hpp"
#include "fdr_policy.hpp"
#include "fdr_record.hpp"
#include "fdr_logs_schema.pb.h"

#include <filesystem>
#include <boost/algorithm/string.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/exception.hpp>


#include "fdr_store.hpp"
#include "dbus_accessor.hpp"
#include "fdr_redfish.hpp"

extern RedfishClient *rfc;

Record::Record(Profile_t &profile, Section_t &section, Component_t &component, InfoGroup_t &infogroup, Info_t &info) : profile(profile), section(section), component(component), infogroup(infogroup), info(info)
{
    logsformat = profile.GeneralConfig.LogsFormat;

    // Only used if encoding type is binary/json
    logdir = profile.GeneralConfig.LogsBasePath + "/" + section.ID + "/" + component.ID + "/";
    logfile = infogroup.ID + ".log";

    if (logsformat == ENCODING_CHOICE_DB){
        logfilepath = profile.GeneralConfig.LogsBasePath + "/" + profile.GeneralConfig.DatabaseName;
    }
    else if (logsformat == ENCODING_CHOICE_BINARY || logsformat == ENCODING_CHOICE_JSON){
        logfilepath = logdir + logfile;
    }
    

    LastFetchedAt = 0; // Init last read time to epoch
    LastStoredAt = 0;  // Init last store time to epoch

    data.set_paramtype(info.DataType); 

    // Search & replace all params with values in the commands/paths
    for (auto &param : component.Params)
    {
        //$param.name --> param.value
        if (info.FetchMethod == "Command")
        {
            boost::replace_all(info.CommandParams.Command, "$" + param.name, param.value);
        }
        else if (info.FetchMethod == "DBUS")
        {
            boost::replace_all(info.DbusParams.Service, "$" + param.name, param.value);
            boost::replace_all(info.DbusParams.ObjectPath, "$" + param.name, param.value);
            boost::replace_all(info.DbusParams.Interface, "$" + param.name, param.value);
            boost::replace_all(info.DbusParams.Property, "$" + param.name, param.value);
        }
    }

    // Create log directory if missing
    std::filesystem::path dir;
    if (logsformat == ENCODING_CHOICE_DB){
        dir = profile.GeneralConfig.LogsBasePath;
    }
    else if (logsformat == ENCODING_CHOICE_JSON || logsformat == ENCODING_CHOICE_BINARY){
        dir = logdir;
    }
    if (!(std::filesystem::exists(dir)))
    {
        if (!(std::filesystem::create_directories(dir)))
            std::cout << "Failed to create directory: " << dir << std::endl;
        // TODO: error handling
    }

    // Init the encoder to file
    //  FDREncoder fdrreaderwriter(logfilepath, "JSON");
    if (logsformat == ENCODING_CHOICE_DB){
        fdrreaderwriter.reset(new FDRStore(logfilepath, logsformat, infogroup.ID, section.ID, component.ID));
    }
    else if (logsformat == ENCODING_CHOICE_JSON || logsformat == ENCODING_CHOICE_BINARY){
        fdrreaderwriter.reset(new FDRStore(logfilepath, profile.GeneralConfig.LogsFormat));
    }
}

void Record::Refresh(void)
{
    // Skip if too early to refresh
    if (difftime(std::time(nullptr), LastFetchedAt) < info.FetchFreqSecs)
        return;

    std::time_t current_time = std::time(nullptr);
    data.set_timestamp(current_time);
    data.set_paramname(info.ID);
    LastFetchedAt = current_time;

    if (info.FetchMethod == "Command")
    {
        CommandResult_t cmdResult = exec(info.CommandParams.Command.c_str());
		if (cmdResult.cmdExitstatus == FDR_ERR_GENFAILURE) {
			std::cout << "command Failed: " << info.CommandParams.Command << std::endl;
			return;
		}

        std::string commandresult = cmdResult.cmdOutput;
        if (data.paramtype() == "Uint64")
        {
            std::istringstream str2num(commandresult);
            uint64_t val;
            str2num >> val;
            data.set_paramvalueint64(val);
        }
        else
        {
            data.set_paramvaluestring(commandresult);
        }
    }
    else if (info.FetchMethod == "DBUS")
    {
        std::cout << "DbusParams Are: ";
        std::cout << info.DbusParams.Service.c_str() << info.DbusParams.ObjectPath.c_str() << info.DbusParams.Interface.c_str() << info.DbusParams.Property.c_str() << std::endl;

        PropertyVariant val = dbus::readDbusProperty(info.DbusParams.Service, info.DbusParams.ObjectPath, 
                                                     info.DbusParams.Interface, info.DbusParams.Property);

        if (data.paramtype() == "Uint64")
        {
            if (auto ptr (std::get_if<int64_t>(&val)); ptr)
            {
                printf("int64 = %" PRIu64 "\n", *ptr);
                data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<uint32_t>(&val)); ptr)
            {
                printf("uint32 = %u\n", *ptr);
                data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<double>(&val)); ptr)
            {
                printf("double = %lf\n", *ptr);
                data.set_paramvalueint64((uint64_t) *ptr);
            }
            else {
                printf("DBus read failed: Unknown numerical variant type\n");
            }
        }
        else
        {
            if (auto ptr (std::get_if<std::string>(&val)); ptr) 
            {
                printf("val =%s\n", ptr->c_str());
                data.set_paramvaluestring(*ptr);
            }
        }
    } else if (info.FetchMethod == "Redfish") {
        if (!rfc) {
            std::cout << "Redfish not configured or not connected, skipping" << std::endl;
            return;
        }
        std::string uri = info.RedfishParams.URI;
        std::string json_pointer = info.RedfishParams.JSONPointer;
        std::cout << "RedfishParams URI: " << uri << " JSONPointer: " << json_pointer << std::endl;

        try {
            if (data.paramtype() == "Uint64") {
                uint64_t u = rfc->query_uint64t (uri, json_pointer);
                data.set_paramvalueint64(u);
            } else {
                // string
                std::string s = rfc->query_string (uri, json_pointer);
                data.set_paramvaluestring(s);
            }
        } catch (const std::exception &e) {
            std::cerr << "Error fetching redfish: " << e.what() << std::endl;
        }
    }
}

bool same_data_values(const fdr::fdr_sample &left, const fdr::fdr_sample &right)
{
    if ((left.paramname() != right.paramname()) || (left.paramtype() != right.paramtype()))
        return false;

    if (left.paramtype() == "Uint64"){
        return (left.paramvalueint64() == right.paramvalueint64());
    }
    else{
        return (left.paramvaluestring() == right.paramvaluestring());
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

void print_data(const std::string name, const fdr::fdr_sample &dat)
{

    std::cout << "Name: " + name << std::endl;
    std::cout << "dat.paramName: " + dat.paramname() << std::endl;
    std::cout << "dat.paramType: " + dat.paramtype() << std::endl;
    if (dat.paramtype() == "Uint64")
        std::cout << "dat.paramValueint64: " + dat.paramvalueint64() << std::endl;
    else
        std::cout << "dat.paramValuestring: " + dat.paramvaluestring() << std::endl;
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
        fdrreaderwriter->append(data);
    }
    else if (logsformat == ENCODING_CHOICE_DB){
        fdr_sample_sql sqlDat;
        sqlDat.timestamp = data.timestamp();
        sqlDat.paramName = data.paramname();
        sqlDat.paramType = data.paramtype();
        if (data.paramtype() == "Uint64"){
            sqlDat.paramValueInt64 = data.paramvalueint64();
        }
        else{
            sqlDat.paramValueString = data.paramvaluestring();
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
        fdr::fdr_sample readrec;
        while (fdrreaderwriter->readnext(&readrec))
        { // TODO: read the file from last to first
            if (readrec.paramname() == info.ID)
            {
                data = last_stored_data = readrec;
            }
        }
    }
    else if (logsformat == ENCODING_CHOICE_DB){
        fdr_sample_sql readrec;
        while (fdrreaderwriter->readnext(&readrec))
        { // TODO: read the file from last to first
            if (readrec.paramName == info.ID)
            {
                data.set_timestamp(readrec.timestamp);
                data.set_paramname(readrec.paramName);
                if (data.paramtype() == "Uint64"){
                    data.set_paramvalueint64(readrec.paramValueInt64);
                }
                else{
                    data.set_paramvaluestring(readrec.paramValueString);
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
}
