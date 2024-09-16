/*
 Copyright (c) 2024, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include "fdr.hpp"

#include "fdr_grp_update.hpp"
#include "fdr_log.hpp"

#include <sys/stat.h>

#include <boost/container/flat_map.hpp>
#include <sdbusplus/bus.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/source/signal.hpp>
#include <stdplus/signal.hpp>

#include <filesystem>
#include <unordered_map>

using sdeventplus::Event;
using sdeventplus::source::Enabled;
using sdeventplus::source::Signal;

std::string bootCounter;
std::string sensorDirTimestamp;
// its a hardcoded value in the code as its compulsarily should be in /tmp
// directory and need not come from PPF yaml file[as there is chance somebody
// could update it to some other directory, like, /mnt/.fdrHmcAlive or
// /emmc/.fdrHmcAlive, etc]
std::string fdrHmcAlivePathName = "/tmp/.fdrHmcAlive";
std::string CommonFdrKeepersDirName = "Bookkeeper";

// FDR file structure layout or file content version control.
// Version 1. multi-level directory structure
// Version 2. zero-level or flat directory structure
const uint32_t currentDataFormatVersion = 2;

std::unordered_map<std::string, Record*>
    FlightDataRecorder_c::subscribedRecListMap;

void sigCb(Signal& signal, const struct signalfd_siginfo*)
{
    signal.get_event().exit(0);
}

FlightDataRecorder_c::FlightDataRecorder_c(const std::string filename)
{
    int retVal;

    if (!filename.empty())
    {
        fdrlog::info("Specified PPF file {}, PPF detection skipped", filename);
        retVal = ConvertPPFToStruct(filename);
        PPFName = filename;
    }
    else
    {
        // have to identify the platform
        retVal = FindAndLoadPlatformProfile();
    }
    if (retVal != FDR_SUCCESS)
    {
        fdrlog::error("Error loading PPF file..Exiting!");
        exit(EXIT_FAILURE);
    }

    // perform a sanity check of the PPF file
    SanityChecker.reset(new PPFSanity());
    retVal = SanityChecker->SanityTestPPF(PPFName);
    if (retVal != FDR_SUCCESS)
    {
        fdrlog::error(
            "Error: PPF failed sanity test! Please fix the above issue(s) in the PPF {}.",
            PPFName);
        exit(EXIT_FAILURE);
    }

    birthCertFilePath = profile.GeneralConfig.LogsBasePath +
                        "/BirthCertificate.tar";

#ifdef FDR_USE_SPDLOG
    fdrlog::InitLogger(profile.GeneralConfig.LoggingLevel,
                       profile.GeneralConfig.LogsBasePath + "/fdr.dat",
                       profile.GeneralConfig.LoggingFileMaxSize,
                       profile.GeneralConfig.LoggingFileNumber);
#else
    fdrlog::InitLogger(profile.GeneralConfig.LoggingLevel);
#endif

    this->InitExceptionRateLimiter();

    this->rfc = nullptr;
    if (!this->profile.GeneralConfig.RedfishSchema.empty())
    {
        try
        {
            this->rfc =
                new RedfishClient(this->profile.GeneralConfig.RedfishSchema,
                                  this->profile.GeneralConfig.RedfishUser,
                                  this->profile.GeneralConfig.RedfishPassword);
        }
        catch (const std::exception& e)
        {
            fdrlog::error("Error creating redfish client: {}", e.what());
        }
    }
    else
    {
        fdrlog::debug(
            "No redfish configuration found, skipped creating redfish client.");
    }

    // Perform platform pre-check conditions
    retVal = ExecutePreconditionRules();
    if (retVal != FDR_SUCCESS)
    {
        fdrlog::error("Error: Platform precheck conditions failed");
        exit(EXIT_FAILURE);
    }

    // check and exit if fdr partion disk availability is less during init time
    CheckFdrPartitionDiskUsageAndExit();

    // update the boot counter
    UpdateGlobVariables(true);

    // update FDR boot event
    UpdateBootEventLog();

    // create a hidden empty file /tmp/.fdrHmcAlive, if it not exist
    CreateFdrHmcAlive();

    fdrlog::debug("bootCounter: {}; sensorDirTimestamp: {}", bootCounter,
                  sensorDirTimestamp);

    birthCertFilePath = profile.GeneralConfig.LogsBasePath + "/" +
                        CommonFdrKeepersDirName + "/BirthCertificate.tar";

    // set the current time
    LastCompactWindowDirCreationSecsAt = std::time(nullptr);
    LastCompactSubWindowDirCreationSecsAt = std::time(nullptr);

    // create the records from the PPF file
    CreateRecords();

    CompactorBookKeeperCleanEntries();
    CompactorBookKeeperAppendEntry();

    // Add signal handler for SIGINT/SIGTERM
    stdplus::signal::block(SIGINT);
    Signal(FdrEvents, SIGINT, sigCb).set_floating(true);
    stdplus::signal::block(SIGTERM);
    Signal(FdrEvents, SIGTERM, sigCb).set_floating(true);
}

void FlightDataRecorder_c::InitExceptionRateLimiter()
{
    this->ExceptionRateLimiter = nullptr;
    if (this->profile.GeneralConfig.ExceptionAllowRate != 0.0)
    {
        this->ExceptionRateLimiter =
            new LeakyBucket(this->profile.GeneralConfig.ExceptionAllowNumber,
                            this->profile.GeneralConfig.ExceptionAllowRate);

        fdrlog::debug("ExpRaterLimiter: Capacity {}, Rate {:f}",
                      this->ExceptionRateLimiter->Capacity(),
                      this->ExceptionRateLimiter->Rate());
    }
    else
    {
        fdrlog::debug(
            "ExceptionAllowRate <= 0.0, ExceptionRateLimiter disabled, FDR will never exit.");
    }
}

void FlightDataRecorder_c::CheckExceptionRateLimit()
{
    if (this->ExceptionRateLimiter)
    {
        auto added = this->ExceptionRateLimiter->Add(1);
        if (added != 1)
        {
            // RateLimiter is full,
            fdrlog::error(
                "ExceptionRateLimiter: Uncaught exceptions exceeded the capacity {}, exiting!!!",
                this->ExceptionRateLimiter->Capacity());
            exit(EXIT_FAILURE);
        }
        else
        {
            fdrlog::debug(
                "ExceptionRateLimiter: Uncaught exceptions {}, capacity {}",
                this->ExceptionRateLimiter->Count(),
                this->ExceptionRateLimiter->Capacity());
        }
    }
}

int FlightDataRecorder_c::FindAndLoadPlatformProfile(void)
{
    // Path to the directory
    std::vector<std::string> SupportedPlatforms;
    struct stat sb;
    int retVal;

    const char* platforms_path =
        getenv("PLATFORMS_PATH"); // Applicable when FDR is running from a
                                  // installed location
    // if the Environment Variable isn't set, we'll look into the most relevant
    // path if/when a developer is running fdr from build directory
    std::string SupportedPlatformsDir =
        (platforms_path != NULL ? platforms_path : "./platforms");
    if (!(std::filesystem::exists(SupportedPlatformsDir)))
    {
        fdrlog::error("Not able to find the 'Platform Profile File' directory");
        return FDR_ERR_GENFAILURE;
    }

    // step 1: get all the PPF files
    // catch any exception while parsing through the yaml or json file
    try
    {
        for (const auto& entry :
             std::filesystem::directory_iterator(SupportedPlatformsDir))
        {
            // Testing whether the path points to a non-directory or not If it
            // does, displays path
            if (stat(entry.path().c_str(), &sb) == 0 && !(sb.st_mode & S_IFDIR))
            {
                SupportedPlatforms.push_back(entry.path());
            }
        }
    }
    catch (std::exception& e)
    {
        fdrlog::error("Exception while iterating PPF directory: {}: {}",
                      SupportedPlatformsDir, e.what());
        return FDR_ERR_GENFAILURE;
    }

    // sort the list of platform files ascendingly
    sort(SupportedPlatforms.begin(), SupportedPlatforms.end());

    // step 2: loop through the list of PPFs, execute the rules and find the
    // right PPF
    for (auto filename : SupportedPlatforms)
    {
        fdrlog::info("Trying {}", filename);

        // HACK: If YAML, convert to JSON because yaml-cpp has trouble parsing
        // yaml with anchors and aliases
        if (filename.substr(filename.find_last_of(".")) != ".yaml")
        {
            fdrlog::info("skipping non config file: {}", filename);
            continue;
        }

        // convert the given PPF to data structs
        retVal = ConvertPPFToStruct(filename);
        if (retVal != FDR_SUCCESS)
        {
            fdrlog::info("ExecuteFingerPrintRules failed...Try another");
            continue;
        }
        // execute the Fingerprint in the PPF
        retVal = ExecuteFingerPrintRules();
        if (retVal == FDR_SUCCESS)
        {
            fdrlog::info("Found the PPF file: {}", filename);
            PPFName = filename;
            // now Data struct have all the values from this PPF file. Hence
            // return FDR_SUCCESS.
            return FDR_SUCCESS;
        }
    }

    fdrlog::error("Not able to find the right PPF file for this platform.");

    return FDR_ERR_GENFAILURE;
}

// convert the Platform Profile File[PPF] file and store the values in the
// "profile" data struct
int FlightDataRecorder_c::ConvertPPFToStruct(const std::string filename)
{
    // catch any exception while parsing through the yaml or json file
    try
    {
        YAML::Node PlatformProfile = YAML::LoadFile(filename);

        profile.FingerPrint =
            PlatformProfile["FingerPrint"].as<FingerPrint_t>();
        profile.Preconditions =
            PlatformProfile["Preconditions"].as<Preconditions_t>();
        profile.GeneralConfig =
            PlatformProfile["GeneralConfig"].as<GeneralConfig_t>();
        profile.Sections =
            PlatformProfile["Sections"].as<std::vector<Section_t>>();

        return EXIT_SUCCESS;
    }
    catch (std::exception& e)
    {
        fdrlog::error("Exception while parsing {}: {}", filename, e.what());
        return FDR_ERR_GENFAILURE;
    }
}

// use the values from "profile" data struct and execute the rules
int FlightDataRecorder_c::ExecuteFingerPrintRules(void)
{
    for (auto CheckRule : profile.FingerPrint.Checks)
    {
        CommandResult_t cmdResult = exec(CheckRule.c_str());
        // std::cout << "ExecuteFingerPrintRules: commandresult: "
        // 		  << "cmdExitstatus: " << cmdResult.cmdExitstatus << std::endl;
        //   << "; cmdOutput: " << cmdResult.cmdOutput << std::endl;
        if (cmdResult.cmdExitstatus != FDR_SUCCESS)
        {
            // if any of the command failed, then this is not the PPF file for
            // this platform
            fdrlog::info("ExecuteFingerPrintRules: command Failed: {}",
                         CheckRule);
            return FDR_ERR_GENFAILURE;
        }
    }
    return FDR_SUCCESS;
}

int FlightDataRecorder_c::CheckAvailableFdrPartitionDiskSize(void)
{
    std::filesystem::path path =
        profile.GeneralConfig.LogsBasePath; // Replace this with the actual path
                                            // you want to check

    try
    {
        // Get available space on the file system containing the specified path
        std::filesystem::space_info space = std::filesystem::space(path);
        size_t availableSpaceMB = space.available / ONE_MB;

        // fdrlog::warn("Available space: {} MB", availableSpaceMB);

        if (availableSpaceMB <= profile.GeneralConfig.PartitionThresoldCheckMB)
        {
            fdrlog::warn(
                "availableSpaceMB {}MB is less than fdr threshold {}MB",
                availableSpaceMB,
                profile.GeneralConfig.PartitionThresoldCheckMB);
            return FDR_ERR_PARTITION_SIZE_LESS;
        }
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        fdrlog::warn("Error getting filesystem space: {}", e.what());
        return FDR_ERR_GENFAILURE;
    }

    return FDR_SUCCESS;
}

// check the FDR disk space usage on every FDR boot and on every compaction
// window expiry. If available free space for FDR is low, then exit 0
void FlightDataRecorder_c::CheckFdrPartitionDiskUsageAndExit(void)
{
    int retVal;

    retVal = CheckAvailableFdrPartitionDiskSize();
    if (retVal != FDR_SUCCESS)
    {
        fdrlog::warn("High disk usage..Exiting FDR!!");
        exit(EXIT_SUCCESS);
    }
}

// Check platform preconditions before running FDR
int FlightDataRecorder_c::ExecutePreconditionRules(void)
{
    bool allPrechecksPassed = true;
    for (auto CheckRule : profile.Preconditions.Checks)
    {
        // for debugging
        // std::cout << "ID: " << CheckRule.ID
        // 		  << "; Command: " << CheckRule.CommandParams.Command
        // 		  << "; Params.name: " << CheckRule.Params.name
        // 		  << "; Params.value: " << CheckRule.Params.value
        // 		  << "; CommandRetryPolicy: " << CheckRule.CommandRetryPolicy
        // 		  << "; ExitOnFailure: " << CheckRule.ExitOnFailure
        // 		  << std::endl;

        FindAndReplaceAll(CheckRule.CommandParams.Command,
                          "$" + CheckRule.Params.name, CheckRule.Params.value);
        // std::cout << "Modified Command: " << CheckRule.CommandParams.Command
        // << std::endl;

        bool precheckPassed = false;
        // Each check should be tried for a threshold limit
        auto startTime = std::chrono::steady_clock::now();
        auto endTime = startTime +
                       std::chrono::seconds(profile.Preconditions.Threshold);
        while (std::chrono::steady_clock::now() < endTime)
        {
            try
            {
                CommandResult_t cmdResult =
                    exec(CheckRule.CommandParams.Command.c_str());
                if (cmdResult.cmdExitstatus == FDR_SUCCESS)
                {
                    precheckPassed = true;
                    break; // Run next check
                }
                else
                {
                    if (CheckRule.CommandRetryPolicy == "false")
                    {
                        break;
                    }
                }
            }
            catch (const std::exception& e)
            {
                fdrlog::error("Failed to run precheck condition error: {}",
                              e.what());
            }
            sleep(1);
        }

        // check what action needs to be done when this precondition failed
        if (precheckPassed == false)
        {
            if (CheckRule.ExitOnFailure == "true")
            {
                fdrlog::error(
                    "Precheck failed: {} and ExitOnFailure is true; Exiting!!!",
                    CheckRule.CommandParams.Command);
                exit(EXIT_FAILURE);
            }
            else if (CheckRule.ExitOnFailure == "false")
            {
                fdrlog::warn(
                    "Precheck failed: {} and ExitOnFailure is false; Continuing!!!",
                    CheckRule.CommandParams.Command);
            }
        }
        allPrechecksPassed = allPrechecksPassed & precheckPassed;
    }

    if (allPrechecksPassed)
    {
        fdrlog::info("All precheck conditions passed, "
                     "FDR continues to collect telemetry");
    }
    else
    {
        fdrlog::warn("Precheck condition failed or threshold reached, "
                     "FDR continues to collect telemetry");
    }
    return FDR_SUCCESS;
}

// This method is used to create writers in the given directory [mostly in the
// Bookkeeper directory]
std::unique_ptr<FDRStore>
    FlightDataRecorder_c::CreateKeeperWriter(const std::string dirName,
                                             const std::string filename)
{
    std::string logsformat = profile.GeneralConfig.LogsFormat;

    // Only used if encoding type is binary/json
    std::string logdir = profile.GeneralConfig.LogsBasePath + "/" + dirName +
                         "/";
    std::string logfile = filename;

    std::string logfilepath;
    logfilepath = logdir + logfile;

    // Create log directory if missing
    std::filesystem::path dir;
    dir = logdir;

    if (!(std::filesystem::exists(dir)))
    {
        if (!(std::filesystem::create_directories(dir)))
            fdrlog::warn("CreateKeeperWriter: Failed to create directory: {}",
                         dir.string());
        // TODO: error handling
    }

    std::unique_ptr<FDRStore> fdrKeepersWriter;
    fdrKeepersWriter.reset(new FDRStore(
        logfilepath, profile.GeneralConfig.LogsFormat, STORE_WRITER));
    return fdrKeepersWriter;
}

// This method is used to create writers in the FDR file structure directory,
// mostly for writing the sample collected as per the fetchpolicy.
void FlightDataRecorder_c::CreateSamplesWriter(
    Profile_t& profile, std::string compClass, std::string compID,
    std::string paramClass, std::string fileExtention,
    std::shared_ptr<FDRStore>& fdrLogWriter,
    const std::string& fileTimestamp = "")
{
    std::string logsformat = profile.GeneralConfig.LogsFormat;

    // Only used if encoding type is binary/json
    std::string logdir =
        profile.GeneralConfig.LogsBasePath; // base directory for logs
    std::string logfile;

    std::string logfilepath; // full filepath of logs
    if (paramClass == "FAULTS")
    {
        // For faults create file name as Event_<event_timestamp>.dat
        logdir += "/" + GetDirectoryName() + "/";
        logfile = compClass + "." + compID + ".Event_" + fileTimestamp +
                  fileExtention;
    }
    else
    {
        logdir += "/" + GetDirectoryName() + "/";
        logfile = compClass + "." + compID + fileExtention;
    }

    logfilepath = logdir + logfile;
    // Create log directory if missing
    std::filesystem::path dir;
    dir = logdir;

    if (!(std::filesystem::exists(dir)))
    {
        if (!(std::filesystem::create_directories(dir)))
        {
            fdrlog::warn("Failed to create directory: {}", dir.string());
            // TODO: error handling
        }
    }
    else
    {
        // std::cout << "directory exist: " << dir << std::endl;
        // std::cout << "\tlogfilepath: " << logfilepath << std::endl;
    }
    fdrLogWriter.reset(new FDRStore(
        logfilepath, profile.GeneralConfig.LogsFormat, STORE_WRITER));
}

void FlightDataRecorder_c::CreateRecords(void)
{
    // create a book keeper log file for Compactor
    CompactorBookKeeperAppender = CreateKeeperWriter(CommonFdrKeepersDirName,
                                                     CompactorBookKeeperName);
    fdrbookoferrorswriter = CreateKeeperWriter(CommonFdrKeepersDirName,
                                               BookOfErrorKeeperName);
    fdrParamsWriter = CreateKeeperWriter(CommonFdrKeepersDirName,
                                         ParamDescKeeperName);

    for (auto& section : profile.Sections)
    {
        // std::cout << "Section.ID: " << section.ID << "\n";
        section.parent_profile = &profile;
        bool ComponentAddedInSchema = false;
        for (auto& component : section.Components)
        {
            // std::cout << "\tComponent.ID: " << component.ID << "\n";
            component.parent_section = &section;
            for (auto& infogroup : component.InfoGroups)
            {
                infogroup.parent_component = &component;

                // create the FDRStore[samples storage file] here itself and
                // use the same FDRStore pointer for all the records on this
                // infogroup
                std::shared_ptr<FDRStore> fdrStoreObj;
                if ((infogroup.ID).find("Sensor.") != std::string::npos)
                {
                    // create the FDRStore[samples storage file for Sensor.*
                    // infogroups] here itself and use the same FDRStore pointer
                    // for all the records for all the Sensor.* infogroups
                    CreateSamplesWriter(profile, section.ID, component.ID,
                                        infogroup.ID, ".sensors.dat",
                                        fdrStoreObj);
                }
                else
                {
                    // create the FDRStore[samples storage file for non Sensor.*
                    // infogroups like Inventory, Config, Error, Status,
                    // Units-MaxAllowedValue and even the AML events also if
                    // any] here itself and use the same FDRStore pointer for
                    // all the records for all the non Sensor.* infogroups
                    CreateSamplesWriter(profile, section.ID, component.ID,
                                        infogroup.ID, ".others.dat",
                                        fdrStoreObj);
                }

                // For AML events store FDRStore objects for devices having
                // `Error` fields
                if ((infogroup.ID).find("Error") != std::string::npos)
                {
                    // Store records for book of errors
                    EventRecord rec;
                    rec.profile = &profile;
                    rec.sectionID = section.ID;
                    rec.componentID = component.ID;
                    rec.infogroupID = infogroup.ID;
                    std::pair<std::shared_ptr<FDRStore>, EventRecord> recPair =
                        std::make_pair(fdrStoreObj, rec);
                    fdrDeviceErrorsWriter[component.ID] = recPair;
                }
                // create writer store object for statistic file only for
                // Average CompactionMethod records
                std::shared_ptr<FDRStore> fdrStatStoreObj;
                if (infogroup.CompactionMethod == "Average")
                {
                    CreateSamplesWriter(profile, section.ID, component.ID,
                                        infogroup.ID, ".sensors.stat.dat",
                                        fdrStatStoreObj);
                }

                for (auto& info : infogroup.InfoList)
                {
                    // Save link to the parent
                    info.parent_infogroup = &infogroup;

                    // Create a record object
                    Record* resource = new Record(profile, section, component,
                                                  fdrStoreObj, fdrStatStoreObj,
                                                  infogroup, info);

                    // Append it to the list
                    RecList.push_back(resource);

                    // based on the fetchtype segregate the resource list as per
                    // its StoreFreqSecs: only for Subscribe fetchtype records
                    // for triggering the Store() FetchFreqSecs: only for Poll
                    // fetchtype records for triggering both Refresh() and
                    // Store()
                    if (info.FetchType == "GroupPoll")
                    {
                        bool keyFound = false;
                        auto recordKey = getGroupPollRecordKey(info);
                        if (!recordKey.empty())
                        {
                            if (RecListGrpPollMap.find(info.FetchFreqSecs) !=
                                RecListGrpPollMap.end())
                            {
                                for (auto& groupRecords :
                                     RecListGrpPollMap[info.FetchFreqSecs])
                                {
                                    auto& [groupKey, records] = groupRecords;
                                    if (groupKey == recordKey)
                                    {
                                        records.push_back(resource);
                                        keyFound = true;
                                        break;
                                    }
                                }
                            }
                            if (!keyFound)
                            {
                                std::vector<Record*> records;
                                records.push_back(resource);
                                fdrlog::debug(
                                    "GroupPoll Inserting new group FetchFreqSecs: {} Key: {}",
                                    info.FetchFreqSecs, recordKey);
                                auto entry = std::make_pair(recordKey, records);
                                RecListGrpPollMap[info.FetchFreqSecs].push_back(
                                    entry);
                            }
                        }
                        else
                        {
                            fdrlog::error(
                                "Unsupported record type GroupPoll with FetchMethod {}",
                                info.FetchMethod);
                        }
                    }
                    else if (info.FetchType == "Subscribe")
                    {
                        RecListSubscribeStoreMap[info.StoreFreqSecs].push_back(
                            resource);
                        auto pair = std::make_pair(info.DbusParams.ObjectPath,
                                                   info.DbusParams.Interface);
                        subscribedPaths.push_back(pair);
                        // Construct unique key to fetch record from
                        // propertyChangedSignal message
                        auto key = getSubsRecordKey(info.DbusParams.ObjectPath,
                                                    info.DbusParams.Interface,
                                                    info.DbusParams.Property);
                        subscribedRecListMap[key] = resource;
                    }
                    else if (info.FetchType == "Poll")
                    {
                        RecListPollMap[info.FetchFreqSecs].push_back(resource);
                    }

                    if (!ComponentAddedInSchema) // Added ONLY ONCE for a
                                                 // component class to avoid
                                                 // repeated entries
                    {
                        // Create fdr_params data
                        fdrpb::fdr_params data;
                        data.set_compclass(section.ID);
                        data.set_paramclass(infogroup.ID);
                        data.set_paramname(info.ID);
                        data.set_paramid(info.ParamID);
                        data.set_paramtype(info.DataType);
                        // data.set_paramunits(info.ID); // TO-DO
                        // data.set_paramnotes(info.ID); // TO-DO
                        fdrParamsWriter->append(data);
                    }
                }

                // std::cout << "CreateRecords: fdrStoreObj.use_count: "
                // 		  << fdrStoreObj.use_count() << std::endl;
            }
            ComponentAddedInSchema = true;
        }
    }
}

void FlightDataRecorder_c::MakeBirthCertificateDeleteSafe(void)
{
    std::string commandStr = "chattr +i " + birthCertFilePath;
    fdrlog::debug("MakeBirthCertificateDeleteSafe: command: {}", commandStr);
    CommandResult_t cmdResult = exec(commandStr.c_str());
    if (cmdResult.cmdExitstatus != FDR_SUCCESS)
    {
        fdrlog::warn(
            "MakeBirthCertificateDeleteSafe: command Failed: {}, cmdExitstatus: {}",
            commandStr, cmdResult.cmdExitstatus);
    }
    fdrlog::debug(
        "MakeBirthCertificateDeleteSafe: Successfull Birth certificate: {}",
        birthCertFilePath);
}

// This method will creates a snapshot of all Inventory.dat, config.dat and
// Versions.dat of all the inventory only for the very first time when fdr
// booted
void FlightDataRecorder_c::CollectAndArchieveBirthCertificate(void)
{
    // 1. execute all the records irrespective of whether birth certificate got
    // created or not
    GroupRefreshAndStore(true);
    RefreshAndStore(true);

    // 2. create the birth certificate archieve, if not already present
    if (!(std::filesystem::exists(birthCertFilePath)))
    {
        std::string fdrDumpPath = profile.GeneralConfig.LogsBasePath;
        std::string commandStr =
            "find " + fdrDumpPath +
            " | grep -e .others.dat -e Bookkeeper | xargs tar -cJf " +
            birthCertFilePath;

        // fdrlog::debug("CollectAndArchieveBirthCertificate: tarCmd: {},
        // commandStr);
        CommandResult_t cmdResult = exec(commandStr.c_str());
        if (cmdResult.cmdExitstatus != FDR_SUCCESS)
        {
            fdrlog::warn("tarCmd command Failed: {}, cmdExitstatus: {}",
                         commandStr, cmdResult.cmdExitstatus);
        }
        fdrlog::debug(
            "CollectAndArchieveBirthCertificate: Successfully created Birth certificate: {}",
            birthCertFilePath);
    }
    else
    {
        fdrlog::debug(
            "CollectAndArchieveBirthCertificate: exist: {}..so no need to create Birthcertificate again!!",
            birthCertFilePath);
    }

    // 3. make the Birthcertificate tar file delete safe
    MakeBirthCertificateDeleteSafe();

    // 4. after creating the birth certificate, remove only the "BootEvent"
    // records.
    //    These records needs to be executed only once when FDR comes up. This
    //    records need not be executed during the FDR runtime.
    // fdrlog::debug("Before bootevent
    // deleting----------------------------------"); fdrlog::debug("size of
    // RecList: {}", RecList.size());
    DeleteSpecificRecords("AfterBootDelete");
    // fdrlog::debug("after bootevent
    // deleting-----------------------------------"); fdrlog::debug("size of
    // RecList: {}", RecList.size());
}

// This method operate on a map of record list which will used both
// fetching[refresh] and storing[store]
void FlightDataRecorder_c::RefreshAndStore(
    bool viaTimerSkipChecks, const std::vector<Record*>& recordListToRefresh)
{
    for (auto& rec : recordListToRefresh)
    {
        bool expt = false;
        try
        {
            rec->Refresh(viaTimerSkipChecks);
            rec->Store();
        }
        catch (const std::exception& e)
        {
            expt = true;
            fdrlog::warn("RefreshAndStore(): {}", e.what());
        }
        catch (...)
        {
            expt = true;
            fdrlog::warn("RefreshAndStore(): unknown exception !!!");
        }

        if (expt)
        {
            this->CheckExceptionRateLimit();
        }
    }
}

void FlightDataRecorder_c::GroupRefreshAndStore(
    [[maybe_unused]] bool viaTimerSkipChecks,
    const std::vector<groupPollRecords>& groupList)
{
    for (auto& grp : groupList)
    {
        bool expt = false;
        try
        {
            // 1. Get Group Refresh Method
            auto keys = splitGroupFetchKeys(grp.first);
            // 2. Refresh data based on the method
            FdrGrpUpdate::RefreshAndStore(keys, grp.second);
        }
        catch (const std::exception& e)
        {
            expt = true;
            fdrlog::warn("GroupRefreshAndStore(): {}", e.what());
        }
        catch (...)
        {
            expt = true;
            fdrlog::warn("GroupRefreshAndStore(): unknown exception !!!");
        }

        if (expt)
        {
            this->CheckExceptionRateLimit();
        }
    }
}

void FlightDataRecorder_c::GroupRefreshAndStore(bool viaTimerSkipChecks)
{
    for (auto& entry : RecListGrpPollMap)
    {
        GroupRefreshAndStore(viaTimerSkipChecks, entry.second);
    }
}

void FlightDataRecorder_c::RefreshAndStore(bool viaTimerSkipChecks)
{
    RefreshAndStore(viaTimerSkipChecks, this->RecList);
}

// This method will only call Store() of the given Subscribe record list as
// refresh would have done by the subscription event signal handler.
void FlightDataRecorder_c::StoreSubscribeRecords(
    const std::vector<Record*>& recordListToStore)
{
    for (auto& rec : recordListToStore)
    {
        bool expt = false;
        try
        {
            rec->Store();
        }
        catch (const std::exception& e)
        {
            expt = true;
            fdrlog::warn("StoreSubscribeRecords(): {}", e.what());
        }
        catch (...)
        {
            expt = true;
            fdrlog::warn("StoreSubscribeRecords(): unknown exception !!!");
        }

        if (expt)
        {
            this->CheckExceptionRateLimit();
        }
    }
}

// create a hidden empty file /tmp/.fdrHmcAlive, if it not exist. This file will
// be used to determine whether this fdr instance is due to HMC boot or fdr
// instance restart. In case of HMC reboot: 		Since HMC is an Embedded
// system, any content written to /tmp directory
//      will be erased after reboot, as it have only ramfs and not have backing
//      disk.
// In case of fdr restart:
// 		* [FDR is a one of the process or service in HMC]
// 		* create /tmp/.fdrHmcAlive hidden file if it does not exists
// 		* Since its only a service in HMC, restart of HMC wont erase the content
// of /tmp
// 		* with this infra, fdr will identify itself, whether the restart of fdr
// is due 		  to HMC reboot or fdr restart[due to any malfunction or crash
// or config change, etc]
// 		* This way of identifying the fdr instance restart is required to get
// the correct 		  boot counter maintained as per the BootEventLog.dat file
// NOTE:
// 1. This way of identifying the fdr restart will work fine only in HMC[as /tmp
// directory 	  content is cleared on every reboot of HMC]
// 2. This wont work in Host, where /tmp is backed with actual disk and the /tmp
// directory 	  content wont be cleared on every Host reboot.
// 3. This function should be called only after "OnBoot records execution".
void FlightDataRecorder_c::CreateFdrHmcAlive(void)
{
    if (!(std::filesystem::exists(fdrHmcAlivePathName)))
    {
        std::fstream file; // object of fstream class

        // create the file
        file.open(fdrHmcAlivePathName, std::ios::out);

        // If file is not created, return error
        if (!file)
        {
            fdrlog::warn("{}: Error in file creation!", fdrHmcAlivePathName);
            // no need to abort/exit FDR instance as it wont create a major
            // functionality break in normal function of fdr itself. Just
            // continue the operation of FDR with a error message in the fdr
            // log.
        }
        else
        {
            // File is created and close it
            file.close();
        }
    }
    else
    {
        fdrlog::debug("{}: already exist!!", fdrHmcAlivePathName);
    }
}

// update the global variable:
//    1. bootCounter: global variable will be updated only once in the FDR init
//    time
//    2. sensorDirTimestamp: global variable will be updated during both FDR
//    init time and during
//                           every CompactionWindowSecs expiry
// dependency: This function should get called before calling
// CreateFdrHmcAlive(), so that the /usr/bin/FdrBootCounter.sh script could
// detect the presence of /tmp/.fdrHmcAlive and get the correct the value of
// bootcounter.
void FlightDataRecorder_c::UpdateGlobVariables(bool needtoUpdateBootcounter)
{
    // bootCounter global variable will be updated only once in the FDR init
    // time
    if (needtoUpdateBootcounter == true)
    {
        std::string bootcounterDir = profile.GeneralConfig.LogsBasePath + "/" +
                                     CommonFdrKeepersDirName + "/";
        std::string bootCountCmd = "/usr/bin/FdrBootCounter.sh " +
                                   bootcounterDir;

        // 1. get the Boot Counter
        CommandResult_t bootCountCmdResult = exec(bootCountCmd.c_str());
        if (bootCountCmdResult.cmdExitstatus != FDR_SUCCESS)
        {
            // if any of the command failed, then this is not the PPF file for
            // this platform
            fdrlog::warn("UpdateGlobVariables: command Failed: {}",
                         bootCountCmd);
            fdrlog::warn(
                "UpdateGlobVariables: commandresult: {}; cmdOutput: {}",
                bootCountCmdResult.cmdExitstatus, bootCountCmdResult.cmdOutput);
        }
        // 2. check if the file exists
        std::string BootCountFilepath = bootcounterDir + "BootCount.txt";
        if (!(std::filesystem::exists(BootCountFilepath)))
        {
            fdrlog::warn("BootCount file Not Exist!!: {}", BootCountFilepath);
            // if the file not exist, then hardcode fixed value to the variable
            bootCounter = "0";
        }
        else
        {
            // 3. get the boot counter from the BootCount.txt
            std::ifstream f(BootCountFilepath);
            f >> bootCounter;
            fdrlog::info("UpdateGlobVariables: bootCounter: {}", bootCounter);
        }
    }

    // 4. get the current time stamp and convert into string
    std::time_t currentTime = std::time(nullptr);
    std::stringstream ss;
    ss << currentTime;
    sensorDirTimestamp = ss.str();
}

// dependency: This function should get called after calling
// UpdateGlobVariables(), so that the bootcounter variable would have got
// updated value.
void FlightDataRecorder_c::UpdateBootEventLog(void)
{
    std::unique_ptr<FDRStore> bootEventWriter;
    bootEventWriter = CreateKeeperWriter(CommonFdrKeepersDirName,
                                         BootEventKeeperName);

    // Create fdr_boot_event data
    fdrpb::fdr_boot_event fdr_boot_event_data;

    std::time_t current_time = std::time(nullptr);
    fdr_boot_event_data.set_eventtimestamp(current_time);
    fdr_boot_event_data.set_bootid(bootCounter);
    fdr_boot_event_data.set_uptime(get_procuptime());
    fdr_boot_event_data.set_datadirformatversion(currentDataFormatVersion);
    bootEventWriter->append(fdr_boot_event_data);

    fdrlog::info("Fdr Data Format Version: {}", currentDataFormatVersion);
}

void FlightDataRecorder_c::DeleteSpecificRecords(std::string recRetentionPolicy)
{
    // for Debugging:
    // std::cout << "Before deleting: " << recRetentionPolicy <<
    // "--------------" << std::endl; std::cout << "size of RecList:" <<
    // RecList.size() << std::endl; for (auto recIt =  RecList.begin(); recIt !=
    // RecList.end(); ++recIt) {
    // 	(*recIt)->Print();
    // }

    // delete the sensor records alone and again create them
    for (auto recIt = RecList.begin(); recIt != RecList.end();)
    {
        if ((*recIt)->infogroup.RecordRetentionPolicy != recRetentionPolicy)
        {
            // std::cout << "skip deleting: " << (*recIt)->infogroup.ID
            // 		  << "; CompactionMethod: " <<
            // (*recIt)->infogroup.CompactionMethod
            //		  << "; RecordRetentionPolicy: " <<
            //(*recIt)->infogroup.RecordRetentionPolicy
            // 		  << std::endl;
            ++recIt;
            continue;
        }

        // delete the record [this will trigger the descructor of the respective
        // object]
        delete (*recIt);

        // then delete the entry from the RecList vector
        RecList.erase(recIt);

        // after deleting the element from the vector, reinitalize the iterator
        // from the begining
        recIt = RecList.begin();
    }
    // for Debugging:
    // std::cout << "after
    // deleting-------------------------------------------------------------------------"
    // << std::endl; std::cout << "size of RecList:" << RecList.size() <<
    // std::endl; std::cout <<
    // "-------------------------------------------------------------------------"
    // << std::endl;
}

// This function will just modify the store pointer for the intended records.
void FlightDataRecorder_c::ModifySpecificRecords(std::string recRetentionPolicy)
{
    // std::cout << "ModifySpecificRecords: 1: numFdsObjs: " << numFdsObjs <<
    // std::endl;

    // step 1: release the existing store pointer
    for (auto& rec : RecList)
    {
        if (rec->infogroup.RecordRetentionPolicy == recRetentionPolicy)
        {
            rec->ResetLogStatStorePtrs();
        }
    }
    // std::cout << "ModifySpecificRecords: 2: numFdsObjs: " << numFdsObjs <<
    // std::endl;

    // step 2: create new store objects and assign them to those specifi record
    // store pointers
    std::vector<std::string> ProcessedSecCompInfoGroupID;
    for (auto& outerRec : RecList)
    {
        std::string SecCompAndInfoGroupID = outerRec->GetSectionID() + "_" +
                                            outerRec->GetComponentID() + "_" +
                                            outerRec->GetInfoGroupID();
        if (outerRec->infogroup.RecordRetentionPolicy == recRetentionPolicy &&
            // check if this Section.ID, Component.ID and infogroup.ID is
            // already processed. If yes, then skip to next record
            std::find(std::begin(ProcessedSecCompInfoGroupID),
                      std::end(ProcessedSecCompInfoGroupID),
                      SecCompAndInfoGroupID) ==
                std::end(ProcessedSecCompInfoGroupID))
        {
            ProcessedSecCompInfoGroupID.push_back(SecCompAndInfoGroupID);

            // create the new FDRStore[samples storage file] here itself and
            // use the same FDRStore pointer for all the records on this
            // Section.ID, Component.ID and infogroup.ID
            std::shared_ptr<FDRStore> fdrStoreObj;
            CreateSamplesWriter(
                profile, outerRec->GetSectionID(), outerRec->GetComponentID(),
                outerRec->GetInfoGroupID(), ".dat", fdrStoreObj);
            if (outerRec->GetInfoGroupID().find("Sensor.") != std::string::npos)
            {
                // create the FDRStore[samples storage file for Sensor.*
                // infogroups] here itself and use the same FDRStore pointer for
                // all the records for all the Sensor.* infogroups
                CreateSamplesWriter(profile, outerRec->GetSectionID(),
                                    outerRec->GetComponentID(),
                                    outerRec->GetInfoGroupID(), ".sensors.dat",
                                    fdrStoreObj);
            }
            else
            {
                // create the FDRStore[samples storage file for non Sensor.*
                // infogroups like Inventory, Config, Error, Status] here itself
                // and use the same FDRStore pointer for all the records for all
                // the non Sensor.* infogroups
                CreateSamplesWriter(profile, outerRec->GetSectionID(),
                                    outerRec->GetComponentID(),
                                    outerRec->GetInfoGroupID(), ".others.dat",
                                    fdrStoreObj);
            }
            // create writer store object for statistic file
            std::shared_ptr<FDRStore> fdrStatStoreObj;
            CreateSamplesWriter(profile, outerRec->GetSectionID(),
                                outerRec->GetComponentID(),
                                outerRec->GetInfoGroupID(), ".sensors.stat.dat",
                                fdrStatStoreObj);

            // assign same store pointer for all other records having same
            // Section.ID, Component.ID and infogroup.ID
            for (auto& innerRec : RecList)
            {
                if (outerRec->GetSectionID() == innerRec->GetSectionID() &&
                    outerRec->GetComponentID() == innerRec->GetComponentID() &&
                    outerRec->GetInfoGroupID() == innerRec->GetInfoGroupID())
                {
                    innerRec->ResetLogStatStorePtrs(fdrStoreObj,
                                                    fdrStatStoreObj);
                }
            }
        }
    }

    // std::cout << "ModifySpecificRecords: 3: numFdsObjs: " << numFdsObjs <<
    // std::endl;
}

void FlightDataRecorder_c::dbusEventHandlerCallback(
    sdbusplus::message::message& msg)
{
    std::string msgInterface;
    boost::container::flat_map<std::string, PropertyVariant> propertiesChanged;

    msg.read(msgInterface, propertiesChanged);

    std::string objectPath = msg.get_path();
    std::string sender = msg.get_sender();

    if (propertiesChanged.empty())
    {
        return;
    }

    for (auto& property : propertiesChanged)
    {
        auto eventProperty = property.first;
        try
        {
            fdrlog::debug("propertiesChanged signal message on objectPath = {},"
                          "interface = {}, property = {}",
                          objectPath, msgInterface, eventProperty);

            // Get unique key
            auto key = getSubsRecordKey(objectPath, msgInterface,
                                        eventProperty);

            if (subscribedRecListMap.find(key) != subscribedRecListMap.end())
            {
                Record* matchedRecord = subscribedRecListMap[key];
                fdrlog::debug(
                    "propertiesChanged signal message record found for"
                    "objectPath = {}, interface = {}, property = {}",
                    objectPath, msgInterface, eventProperty);

                std::uint64_t val = 0;
                std::string value;

                if (auto ptr(std::get_if<uint32_t>(&property.second)); ptr)
                {
                    val = (uint64_t)*ptr;
                }
                else if (auto ptr(std::get_if<int64_t>(&property.second)); ptr)
                {
                    val = (uint64_t)*ptr;
                }
                else if (auto ptr(std::get_if<uint64_t>(&property.second)); ptr)
                {
                    val = (uint64_t)*ptr;
                }
                else if (auto ptr(std::get_if<uint16_t>(&property.second)); ptr)
                {
                    val = (uint64_t)*ptr;
                }
                else if (auto ptr(std::get_if<int16_t>(&property.second)); ptr)
                {
                    val = (uint64_t)*ptr;
                }
                else if (auto ptr(std::get_if<double>(&property.second)); ptr)
                {
                    val = (uint64_t)*ptr;
                }
                else if (auto ptr(std::get_if<bool>(&property.second)); ptr)
                {
                    val = (uint64_t)*ptr;
                }
                else if (auto ptr(std::get_if<uint8_t>(&property.second)); ptr)
                {
                    val = (uint64_t)*ptr;
                }
                else if (auto ptr =
                             (std::get_if<std::tuple<bool, unsigned int>>(
                                 &property.second));
                         ptr)
                {
                    val = std::get<1>(*ptr);
                }
                else if (auto ptr(std::get_if<std::string>(&property.second));
                         ptr)
                {
                    const char* value1 = ptr->c_str();
                    value = value1;
                }
                else
                {
                    return;
                }

                if (val)
                {
                    matchedRecord->refreshDataCallback(val);
                }
                else if (!(value.size() == 0))
                {
                    matchedRecord->refreshDataCallback(value);
                }
            }
        }
        catch (const std::exception& e)
        {
            fdrlog::warn(
                "Error in processing propertiesChanged signal message: error = {}",
                e.what());
        }
    }
}

void FlightDataRecorder_c::initRecordsSignalRegistration()
{
    auto& bus = getBus();
    // Register callback on unique DBUS paths
    std::set<std::pair<std::string, std::string>> uniqueStrings(
        subscribedPaths.begin(), subscribedPaths.end());
    std::vector<std::pair<std::string, std::string>> uniqueVector(
        uniqueStrings.begin(), uniqueStrings.end());

    fdrlog::info("Registering properties changed signal watchers");

    for (const auto& obj : uniqueVector)
    {
        auto objPath = obj.first;
        auto interface = obj.second;

        auto genericHandler =
            std::bind(&FlightDataRecorder_c::dbusEventHandlerCallback,
                      std::placeholders::_1);

        eventHandlerMatcher.push_back(dbus::registerServicePropertyChanged(
            bus, objPath, interface, genericHandler));
    }
}

FlightDataRecorder_c::~FlightDataRecorder_c()
{
    if (this->rfc)
    {
        delete this->rfc;
    }

    PPFSanity* fds = SanityChecker.release();
    delete fds;
}

void FlightDataRecorder_c::initEventsSignalRegistration()
{
    auto objPath = profile.GeneralConfig.eventParams.objectPath;
    auto intf = profile.GeneralConfig.eventParams.interface;
    auto member = profile.GeneralConfig.eventParams.member;

    // Register AML events watcher only for supported platforms
    if (!objPath.empty() && !intf.empty() && !member.empty())
    {
        fdrlog::info("Registering events signal watcher");
        EventSignalHandler* eventHandler = new EventSignalHandler(
            objPath, intf, member, fdrDeviceErrorsWriter);
        eventHandler->registerEventsSignal();
    }
}
