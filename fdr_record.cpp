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

void CreateLog(Profile_t &profile, std::unique_ptr<FDRStore> &fdrLogWriter, std::string paramClass, std::string compClass, std::string compID) {
	std::string logsformat = profile.GeneralConfig.LogsFormat;

	// Only used if encoding type is binary/json
    std::string logdir = profile.GeneralConfig.LogsBasePath + "/" + compClass + "/" + compID + "/"; // base directory for logs
    std::string logfile = paramClass + ".log"; // relative filename of logs

    std::string logfilepath; // full filepath of logs
	if (logsformat == ENCODING_CHOICE_DB){
        logfilepath = profile.GeneralConfig.LogsBasePath + "/" + profile.GeneralConfig.DatabaseName;
    }
    else if (logsformat == ENCODING_CHOICE_BINARY || logsformat == ENCODING_CHOICE_JSON){
        logfilepath = logdir + logfile;
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

    if (logsformat == ENCODING_CHOICE_DB){
        fdrLogWriter.reset(new FDRStore(logfilepath, logsformat, paramClass, compClass, compID));
    }
    else if (logsformat == ENCODING_CHOICE_JSON || logsformat == ENCODING_CHOICE_BINARY){
        fdrLogWriter.reset(new FDRStore(logfilepath, profile.GeneralConfig.LogsFormat));
    }
}

Record::Record(Profile_t &profile, Section_t &section, Component_t &component, InfoGroup_t &infogroup, Info_t &info) : profile(profile), section(section), component(component), infogroup(infogroup), info(info)
{   
    LastFetchedAt = 0; // Init last read time to epoch
    LastStoredAt = 0;  // Init last store time to epoch

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

    logsformat = profile.GeneralConfig.LogsFormat;

    std::string paramClass = infogroup.ID;
    std::string compClass = section.ID;
    std::string compID = component.ID;

    CreateLog(profile, fdrreaderwriter, paramClass, compClass, compID);

    // for debugging purpose
    // Print();
}

Record::~Record()
{
    // data.release_paramtype();
    FDRStore* fds = fdrreaderwriter.release();
    delete fds;
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

/*
    std::cout << "Execute: current_time: " << current_time << std::endl
              << "\tsection.ID: " << section.ID << std::endl
              << "\tComponent.ID: " << component.ID << std::endl
              << "\t\tinfogroup.ID: " << infogroup.ID<< std::endl
              << "\t\t\tinfo.ID: " << info.ID<< std::endl;
*/

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
        std::cout << "DbusParams Are: " << std::endl
                  << "\tService: " << info.DbusParams.Service.c_str() << std::endl
                  << "\tObjectPath: " << info.DbusParams.ObjectPath.c_str() << std::endl
                  << "\tInterface: " << info.DbusParams.Interface.c_str() << std::endl
                  << "\tProperty: " << info.DbusParams.Property.c_str() << std::endl;

        PropertyVariant val = dbus::readDbusProperty(info.DbusParams.Service, info.DbusParams.ObjectPath, 
                                                     info.DbusParams.Interface, info.DbusParams.Property);

         if (data.paramtype == "Uint64")
        {
            if (auto ptr (std::get_if<int64_t>(&val)); ptr)
            {
                printf("int64 = %ld\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<uint32_t>(&val)); ptr)
            {
                printf("uint32 = %u\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<uint64_t>(&val)); ptr)
            {
                printf("uint64 = %lu\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<uint16_t>(&val)); ptr)
            {
                printf("uint16 = %u\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<int16_t>(&val)); ptr)
            {
                printf("int16 = %d\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<double>(&val)); ptr)
            {
                printf("double = %lf\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
            }
            else if (auto ptr (std::get_if<bool>(&val)); ptr)
            {
                printf("bool = %d\n", *ptr);
                data.fdr_sample_data.set_paramvalueint64((uint64_t) *ptr);
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
                data.fdr_sample_data.set_paramvaluestring(*ptr);
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
            if (data.paramtype == "Uint64") {
                uint64_t u = rfc->query_uint64t (uri, json_pointer);
                data.fdr_sample_data.set_paramvalueint64(u);
            } else {
                // string
                std::string s = rfc->query_string (uri, json_pointer);
                data.fdr_sample_data.set_paramvaluestring(s);
            }
        } catch (const std::exception &e) {
            std::cerr << "Error fetching redfish: " << e.what() << std::endl;
        }
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
        fdr::fdr_sample readrec;
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
    std::cout << "section.ID: " << section.ID << std::endl
              << "\tComponent.ID: " << component.ID << std::endl
              << "\t\tinfogroup.ID: " << infogroup.ID << std::endl
              << "\t\t\tCompactionPolicy: " << infogroup.CompactionPolicy << std::endl
              << "\t\t\tCompactionMethod: " << infogroup.CompactionMethod << std::endl
              << "\t\t\tCompactionFreqSecs: " << infogroup.CompactionFreqSecs << std::endl
              << "\t\t\tLastCompactedAt: " << infogroup.LastCompactedAt << std::endl
              << "\t\t\tCompactionPolicy: " << infogroup.CompactionPolicy << std::endl
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
