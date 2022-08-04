#include <iostream>
#include <fstream>
#include "fdr_policy.hpp"
#include "fdr_record.hpp"

#include <filesystem>
#include <boost/algorithm/string.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/exception.hpp>


#include "fdr_store.hpp"
#include "dbus_accessor.hpp"

std::string exec(const char *cmd);

Record::Record(Profile_t &profile, Section_t &section, Component_t &component, InfoGroup_t &infogroup, Info_t &info) : profile(profile), section(section), component(component), infogroup(infogroup), info(info)
{
    logsformat = profile.GeneralConfig.LogsFormat;
    logfilepath = profile.GeneralConfig.LogsBasePath + "/" + profile.GeneralConfig.DatabaseName;

    LastFetchedAt = 0; // Init last read time to epoch
    LastStoredAt = 0;  // Init last store time to epoch

    data.paramType = info.DataType;

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
    //std::filesystem::path dir(logdir);
    std::filesystem::path dir(profile.GeneralConfig.LogsBasePath);
    if (!(std::filesystem::exists(dir)))
    {
        if (!(std::filesystem::create_directories(dir)))
            std::cout << "Failed to create directory: " << dir << std::endl;
        // TODO: error handling
    }

    // Init the encoder to file
    //  FDREncoder fdrreaderwriter(logfilepath, "JSON");
    
    //fdrreaderwriter.reset(new FDRStore(logfilepath, profile.GeneralConfig.LogsFormat));
    fdrreaderwriter.reset(new FDRStore(logfilepath, logsformat, infogroup.ID, section.ID, component.ID));
}

void Record::Refresh(void)
{
    // Skip if too early to refresh
    if (difftime(std::time(nullptr), LastFetchedAt) < info.FetchFreqSecs)
        return;

    std::time_t current_time = std::time(nullptr);
    data.timestamp = current_time;
    data.paramName = info.ID;
    LastFetchedAt = current_time;

    if (info.FetchMethod == "Command")
    {
        std::string commandresult = exec(info.CommandParams.Command.c_str());
        if (data.paramType == "Uint64")
        {
            std::istringstream str2num(commandresult);
            uint64_t val;
            str2num >> val;
            data.paramValueInt64 = val;
        }
        else
        {
            data.paramValueString = commandresult;
        }
    }
    else if (info.FetchMethod == "DBUS")
    {
        std::cout << "DbusParams Are: ";
        std::cout << info.DbusParams.Service.c_str() << info.DbusParams.ObjectPath.c_str() << info.DbusParams.Interface.c_str() << info.DbusParams.Property.c_str() << std::endl;

        PropertyVariant val = dbus::readDbusProperty(info.DbusParams.Service, info.DbusParams.ObjectPath, 
                                                     info.DbusParams.Interface, info.DbusParams.Property);

        if (data.paramType == "Uint64")
        {
            if (auto ptr (std::get_if<int64_t>(&val)); ptr)
            {
                printf("int64 = %lld\n", *ptr);
                data.paramValueInt64 = (uint64_t) *ptr;
            }
            else if (auto ptr (std::get_if<uint32_t>(&val)); ptr)
            {
                printf("uint32 = %lu\n", *ptr);
                data.paramValueInt64 = (uint64_t) *ptr;
            }
            else if (auto ptr (std::get_if<double>(&val)); ptr)
            {
                printf("double = %lf\n", *ptr);
                data.paramValueInt64 = (uint64_t) *ptr;
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
                data.paramValueString = *ptr;
            }
        }
    }
}

bool same_data_values(const fdr_sample &left, const fdr_sample &right)
{
    if ((left.paramName != right.paramName) || (left.paramType != right.paramType))
        return false;

    if (left.paramType == "Uint64"){
        return (left.paramValueInt64 == right.paramValueInt64);
    }
    else{
        return (left.paramValueString == right.paramValueString);
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

void print_data(const std::string name, const fdr_sample &dat)
{

    std::cout << "Name: " + name << std::endl;
    std::cout << "dat.paramName: " + dat.paramName << std::endl;
    std::cout << "dat.paramType: " + dat.paramType << std::endl;
    if (dat.paramType == "Uint64")
        std::cout << "dat.paramValueint64: " + dat.paramValueInt64 << std::endl;
    else
        std::cout << "dat.paramValuestring: " + dat.paramValueString << std::endl;
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

    fdrreaderwriter->append(data);

    // if (info.ID == "Model")
    //     std::cout << "Setting last_stored_data = data for ID " << info.parent_infogroup->parent_component->ID << std::endl;
    last_stored_data = data;
    LastStoredAt = std::time(nullptr);
}

// Read the last record of our type from the storage
void Record::Load(void)
{
    fdr_sample readrec;
    while (fdrreaderwriter->readnext(&readrec))
    { // TODO: read the file from last to first
        if (readrec.paramName == info.ID)
        {
            data = last_stored_data = readrec;
        }
    }

    // last_stored_data.paramValue = 0;
}

void Record::Print(void)
{
    //std::cout << "Set last_stored_data.paramValue=" + last_stored_data.paramValueString << std::endl;
}
