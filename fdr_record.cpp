
#include <iostream>
#include <fstream>
#include "fdr_policy.hpp"
#include "fdr_record.hpp"
#include "fdr_logs_schema.pb.h"

#include <filesystem>
#include <boost/algorithm/string.hpp>
#include <systemd/sd-bus.h>

#include "fdr_store.hpp"

std::string exec(const char *cmd);

Record::Record(Profile_t &profile, Section_t &section, Component_t &component, InfoGroup_t &infogroup, Info_t &info) : profile(profile), section(section), component(component), infogroup(infogroup), info(info)
{
    logsformat = profile.GeneralConfig.LogsFormat;
    logdir = profile.GeneralConfig.LogsBasePath + "/" + section.ID + "/" + component.ID + "/";
    logfile = infogroup.ID + ".log";
    logfilepath = logdir + logfile;

    LastFetchedAt = 0; // Init last read time to epoch
    LastStoredAt = 0;  // Init last store time to epoch

    info.StoreFreqSecs = 5; // HACK: must come from policy file
    data.set_infotype(info.DataType);

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
    std::filesystem::path dir(logdir);
    if (!(std::filesystem::exists(dir)))
    {
        if (!(std::filesystem::create_directories(dir)))
            std::cout << "Failed to create directory: " << dir << std::endl;
        // TODO: error handling
    }

    // Init the encoder to file
    //  FDREncoder fdrreaderwriter(logfilepath, "JSON");
    fdrreaderwriter.reset(new FDRStore(logfilepath, profile.GeneralConfig.LogsFormat));
}

void Record::Refresh(void)
{
    if (difftime(std::time(nullptr), LastFetchedAt) >= info.FetchFreqSecs)
    {
        prev_data = data;

        std::time_t current_time = std::time(nullptr);
        data.set_timestamp(current_time);
        data.set_infoname(info.ID);
        LastFetchedAt = current_time;

        if (info.FetchMethod == "Command")
        {
            data.set_infovaluestring(exec(info.CommandParams.Command.c_str()));
        }
        else if (info.FetchMethod == "DBUS")
        {
            extern sd_bus *bus;
            sd_bus_error error = SD_BUS_ERROR_NULL;
            sd_bus_message *reply = NULL;
            int r;

            std::cout << "DbusParams Are: ";
            std::cout << info.DbusParams.Service << info.DbusParams.ObjectPath << info.DbusParams.Interface << info.DbusParams.Property << std::endl;

            r = sd_bus_get_property(bus, info.DbusParams.Service.c_str(), info.DbusParams.ObjectPath.c_str(),
                                    info.DbusParams.Interface.c_str(), info.DbusParams.Property.c_str(),
                                    &error, &reply, "t");
            if (r < 0)
            {
                printf("sd_bus_get_property failed: error=%s\n", error.message);
            }

            int64_t val;
            r = sd_bus_message_read(reply, "t", &val);
            if (r < 0)
                printf("sd_bus_message_read failed\n");

            printf("val =%ld\n", val);

            if (data.infotype() == "Uint64")
            {
                data.set_infovalueint64(val);
            }
            else
            {
                data.set_infovaluestring(std::to_string(val));
            }

            sd_bus_error_free(&error);
        }

        // TODO: error handling
    }
}

bool same_data_values(const fdr_sample &left, const fdr_sample &right)
{
    if ((left.infoname() != right.infoname()) || (left.infotype() != right.infotype()) || (left.InfoValue_case() != right.InfoValue_case()))
        return false;

    switch (left.InfoValue_case())
    {
    case fdr::fdr_sample::kInfoValueInt64:
        return (left.infovalueint64() == right.infovalueint64());
        break;
    case fdr::fdr_sample::kInfoValueString:
        return (left.infovaluestring() == right.infovaluestring());
        break;
    case 2:
        return true;
        break;
    default:
        return true;
    }
}


void Record::Store(void)
{
    // Skip if update not necessary per the policy
    if ((info.StorePolicy == "OnChange") && (same_data_values(data, prev_data)))
    {
        return;
    }

    // Skip if its not time to store yet
    if (difftime(std::time(nullptr), LastStoredAt) < info.StoreFreqSecs)
    {
        return;
    }

    fdrreaderwriter->append(data);
    LastStoredAt = std::time(nullptr);
}

// Read the last record of our type from the storage
void Record::Load(void)
{
    fdr_sample readrec;
    while (fdrreaderwriter->readnext(&readrec))
    { // TODO: read the file from last to first
        if (readrec.infoname() == info.ID)
        {
            data = prev_data = readrec;
        }
    }

    // prev_data.InfoValue = 0;
}

void Record::Print(void)
{
    // std::cout << "Set prev_data.InfoValue=" + prev_data.InfoValue << std::endl;
}
