/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include <filesystem>
#include <sys/stat.h>
#include "fdr.hpp"

std::string bootCounter;
std::string sensorDirTimestamp;
// its a hardcoded value in the code as its compulsarily should be in /tmp directory
// and need not come from PPF yaml file[as there is chance somebody could update 
// it to some other directory, like, /mnt/.fdrHmcAlive or /emmc/.fdrHmcAlive, etc]
std::string fdrHmcAlivePathName = "/tmp/.fdrHmcAlive"; 
std::string CommonFdrKeepersDirName = "Bookkeeper";

FlightDataRecorder_c::FlightDataRecorder_c(const std::string filename)
{
	int retVal;

	if (!filename.empty())
	{
		std::cout << "Specified PPF file " << filename << ", PPF detection skipped" << std::endl;
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
		std::cout << "Error loading PPF file..Exiting!" << std::endl;
		exit(EXIT_FAILURE);
	}

	// perform a sanity check of the PPF file
	SanityChecker.reset(new PPFSanity());
	retVal = SanityChecker->SanityTestPPF(PPFName);
	if (retVal != FDR_SUCCESS)
	{
		std::cout << "Error: PPF failed sanity test! Please fix the above issue(s) in the PPF " << PPFName << "." << std::endl;
		exit(EXIT_FAILURE);
	}

	birthCertFilePath = profile.GeneralConfig.LogsBasePath + "/BirthCertificate.tar";

	spdlog::level::level_enum level = spdlog::level::from_str(profile.GeneralConfig.LoggingLevel);
	this->log = spdlog::rotating_logger_mt("fdr",
										   this->profile.GeneralConfig.LogsBasePath + "/fdr.log",
										   this->profile.GeneralConfig.LoggingFileMaxSize,
										   this->profile.GeneralConfig.LoggingFileNumber);
	this->log->flush_on(level);
	this->log->set_level(level);

	this->InitExceptionRateLimiter();

	this->rfc = nullptr;
	if (!this->profile.GeneralConfig.RedfishSchema.empty())
	{
		try
		{
			this->rfc = new RedfishClient(this->profile.GeneralConfig.RedfishSchema,
										  this->profile.GeneralConfig.RedfishUser,
										  this->profile.GeneralConfig.RedfishPassword);
		}
		catch (const std::exception &e)
		{
			this->log->error("Error creating redfish client: {}", e.what());
		}
	}
	else
	{
		this->log->info("No redfish configuration found, skipped creating redfish client.");
	}

	// update the boot counter
	UpdateGlobVariables(true);

	// create a hidden empty file /tmp/.fdrHmcAlive, if it not exist 
	CreateFdrHmcAlive();

	std::cout << "bootCounter: " << bootCounter << "; sensorDirTimestamp: " << sensorDirTimestamp << std::endl;

    birthCertFilePath = profile.GeneralConfig.LogsBasePath + "/" + CommonFdrKeepersDirName +"/BirthCertificate.tar";

	// set the current time
	LastCompactWindowDirCreationSecsAt = std::time(nullptr);

	// create the records from the PPF file
	CreateRecords();

	CompactorBookKeeperCleanEntries();
	CompactorBookKeeperAppendEntry();
}

void FlightDataRecorder_c::InitExceptionRateLimiter()
{
	this->ExceptionRateLimiter = nullptr;
	if (this->profile.GeneralConfig.ExceptionAllowRate != 0.0) {
		this->ExceptionRateLimiter = new LeakyBucket(
			this->profile.GeneralConfig.ExceptionAllowNumber,
			this->profile.GeneralConfig.ExceptionAllowRate);

		this->log->debug("ExpRaterLimiter: Capacity {}, Rate {:f}",
			this->ExceptionRateLimiter->Capacity(), this->ExceptionRateLimiter->Rate() );
	} else {
		this->log->debug("ExceptionAllowRate <= 0.0, ExceptionRateLimiter disabled, FDR will never exit.");
	}

}

void FlightDataRecorder_c::CheckExceptionRateLimit() {
	if (this->ExceptionRateLimiter) {
		auto added = this->ExceptionRateLimiter->Add(1);
		if (added != 1) {
			// RateLimiter is full,
			this->log->error("ExceptionRateLimiter: Uncaught exceptions exceeded the capacity {}, exiting!!!", this->ExceptionRateLimiter->Capacity());
			exit(EXIT_FAILURE);
		} else {
			this->log->debug("ExceptionRateLimiter: Uncaught exceptions {}, capacity {}", this->ExceptionRateLimiter->Count(), this->ExceptionRateLimiter->Capacity());
		}
	}
}

int FlightDataRecorder_c::FindAndLoadPlatformProfile(void)
{
	// Path to the directory
	std::vector<std::string> SupportedPlatforms;
	struct stat sb;
	int retVal;

	const char *platforms_path = getenv("PLATFORMS_PATH"); // Applicable when FDR is running from a installed location
	// if the Environment Variable isn't set, we'll look into the most relevant path if/when a developer is running fdr from build directory
	std::string SupportedPlatformsDir = (platforms_path != NULL ? platforms_path : "./platforms");
	if (!(std::filesystem::exists(SupportedPlatformsDir)))
	{
		std::cout << "Not able to find the 'Platform Profile File' directory" << std::endl;
		return FDR_ERR_GENFAILURE;
	}

    // step 1: get all the PPF files
	// catch any exception while parsing through the yaml or json file
	try {
		for (const auto& entry : std::filesystem::directory_iterator(SupportedPlatformsDir)) {
			// Testing whether the path points to a non-directory or not If it does, displays path
			if (stat(entry.path().c_str(), &sb) == 0 && !(sb.st_mode & S_IFDIR)) {
				SupportedPlatforms.push_back(entry.path());
			}
		}
	} catch(std::exception &e) {
		std::cout << "Exception while iterating PPF directory: " << SupportedPlatformsDir << ": " << e.what() << std::endl;
		return FDR_ERR_GENFAILURE;
	}

	// sort the list of platform files ascendingly
	sort(SupportedPlatforms.begin(), SupportedPlatforms.end());

	// step 2: loop through the list of PPFs, execute the rules and find the right PPF
	for (auto filename : SupportedPlatforms)
	{

		std::cout << "Trying " + filename << std::endl;

		// HACK: If YAML, convert to JSON because yaml-cpp has trouble parsing yaml with anchors and aliases
		if (filename.substr(filename.find_last_of(".")) != ".yaml")
		{
			std::cout << "skipping non config file: " << filename << std::endl;
			continue;
		}

		// convert the given PPF to data structs
		retVal = ConvertPPFToStruct(filename);
		if (retVal != FDR_SUCCESS)
		{
			std::cout << "ExecuteFingerPrintRules failed...Try another" << std::endl;
			continue;
		}
		// execute the Fingerprint in the PPF
		retVal = ExecuteFingerPrintRules();
		if (retVal == FDR_SUCCESS)
		{
			std::cout << "Found the PPF file: " << filename << std::endl;
			PPFName = filename;
			// now Data struct have all the values from this PPF file. Hence return FDR_SUCCESS.
			return FDR_SUCCESS;
		}
	}

	std::cout << "Not able to find the right PPF file for this platform." << std::endl;

	return FDR_ERR_GENFAILURE;
}

// convert the Platform Profile File[PPF] file and store the values in the "profile" data struct
int FlightDataRecorder_c::ConvertPPFToStruct(const std::string filename)
{
	// catch any exception while parsing through the yaml or json file
	try
	{
		YAML::Node PlatformProfile = YAML::LoadFile(filename);

		profile.FingerPrint = PlatformProfile["FingerPrint"].as<FingerPrint_t>();
		profile.GeneralConfig = PlatformProfile["GeneralConfig"].as<GeneralConfig_t>();
		profile.Sections = PlatformProfile["Sections"].as<std::vector<Section_t>>();

		return EXIT_SUCCESS;
	}
	catch (std::exception &e)
	{
		std::cout << "Exception while parsing " << filename << ": " << e.what() << std::endl;
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
			// if any of the command failed, then this is not the PPF file for this platform
			std::cout << "ExecuteFingerPrintRules: command Failed: " << CheckRule << std::endl;
			return FDR_ERR_GENFAILURE;
		}
	}
	return FDR_SUCCESS;
}

// This method is used to create writers in the given directory [mostly in the Bookkeeper directory]
std::unique_ptr<FDRStore> FlightDataRecorder_c::CreateKeeperWriter(const std::string dirName, const std::string filename)
{
	std::string logsformat = profile.GeneralConfig.LogsFormat;

    // Only used if encoding type is binary/json
    std::string logdir = profile.GeneralConfig.LogsBasePath + "/" + dirName + "/";
    std::string logfile = filename;

	std::string logfilepath;
    if (logsformat == ENCODING_CHOICE_DB) {
        logfilepath = profile.GeneralConfig.LogsBasePath + "/" + profile.GeneralConfig.DatabaseName;
    } else if (logsformat == ENCODING_CHOICE_BINARY || logsformat == ENCODING_CHOICE_JSON) {
        logfilepath = logdir + logfile;
    }
	// Create log directory if missing
    std::filesystem::path dir;
    if (logsformat == ENCODING_CHOICE_DB) {
        dir = profile.GeneralConfig.LogsBasePath;
    } else if (logsformat == ENCODING_CHOICE_JSON || logsformat == ENCODING_CHOICE_BINARY) {
        dir = logdir;
    }

    if (!(std::filesystem::exists(dir))) {
        if (!(std::filesystem::create_directories(dir)))
            std::cout << "CreateKeeperWriter: Failed to create directory: " << dir << std::endl;
        // TODO: error handling
    }

    std::unique_ptr<FDRStore> fdrKeepersWriter;
    if (logsformat == ENCODING_CHOICE_DB) {
        fdrKeepersWriter.reset(new FDRStore(logfilepath, logsformat, "Param", "Description", "Table")); // TO-DO: CHANGE TABLENAME!!!
    } else if (logsformat == ENCODING_CHOICE_JSON || logsformat == ENCODING_CHOICE_BINARY) {
        fdrKeepersWriter.reset(new FDRStore(logfilepath, profile.GeneralConfig.LogsFormat, STORE_WRITER));
    }
	return fdrKeepersWriter;
}

// This method is used to create writers in the FDR file structure directory, 
// mostly for writing the sample collected as per the fetchpolicy.
void FlightDataRecorder_c::CreateSamplesWriter(Profile_t &profile, std::string compClass, std::string compID,
				std::string paramClass, std::shared_ptr<FDRStore> &fdrLogWriter) {
	std::string logsformat = profile.GeneralConfig.LogsFormat;

	// Only used if encoding type is binary/json
    std::string logdir = profile.GeneralConfig.LogsBasePath; // base directory for logs
    std::string logfile = paramClass + ".log"; // relative filename of logs

    std::string logfilepath; // full filepath of logs
	if (logsformat == ENCODING_CHOICE_DB) {
        logfilepath = profile.GeneralConfig.LogsBasePath + "/" + profile.GeneralConfig.DatabaseName;
    }
    else if (logsformat == ENCODING_CHOICE_BINARY || logsformat == ENCODING_CHOICE_JSON) {
        // let the BootEvent record entries go to common directory "BookKeeper"
        if (paramClass == "BootEvent") {
            logdir += "/" + CommonFdrKeepersDirName + "/";
        } else {
            logdir += "/" + GetDirectoryName() + "/";
            logdir += compClass + "/" + compID + "/";
        }
        logfile = paramClass + ".log";
        logfilepath = logdir + logfile;
    }
	// Create log directory if missing
    std::filesystem::path dir;
    if (logsformat == ENCODING_CHOICE_DB) {
        dir = profile.GeneralConfig.LogsBasePath;
    }
    else if (logsformat == ENCODING_CHOICE_JSON || logsformat == ENCODING_CHOICE_BINARY) {
        dir = logdir;
    }
    if (!(std::filesystem::exists(dir))) {
        if (!(std::filesystem::create_directories(dir))) {
            std::cout << "Failed to create directory: " << dir << std::endl;
            // TODO: error handling
        }
    } else {
            // std::cout << "directory exist: " << dir << std::endl;
            // std::cout << "\tlogfilepath: " << logfilepath << std::endl;
    }

    if (logsformat == ENCODING_CHOICE_DB) {
        fdrLogWriter.reset(new FDRStore(logfilepath, logsformat, compClass, compID, paramClass));
    }
    else if (logsformat == ENCODING_CHOICE_JSON || logsformat == ENCODING_CHOICE_BINARY){
        fdrLogWriter.reset(new FDRStore(logfilepath, profile.GeneralConfig.LogsFormat, STORE_WRITER));
    }
}

void FlightDataRecorder_c::CreateRecords(void)
{
	// create a book keeper log file for Compactor
	CompactorBookKeeperAppender = CreateKeeperWriter(CommonFdrKeepersDirName, CompactorBookKeeperName);
    fdrbookoferrorswriter = CreateKeeperWriter(CommonFdrKeepersDirName, BookOfErrorKeeperName);
	fdrParamsWriter = CreateKeeperWriter(CommonFdrKeepersDirName, ParamDescKeeperName);

	for (auto &section : profile.Sections)
	{
		// std::cout << "Section.ID: " << section.ID << "\n";
		section.parent_profile = &profile;
		for (auto &component : section.Components)
		{
			// std::cout << "\tComponent.ID: " << component.ID << "\n";
			component.parent_section = &section;
			for (auto &infogroup : component.InfoGroups)
			{
				infogroup.parent_component = &component;

				// create the FDRStore[samples storage file] here itself and
				// use the same FDRStore pointer for all the records on this infogroup
			    std::shared_ptr<FDRStore> fdrStoreObj;
			    CreateSamplesWriter(profile, section.ID, component.ID, infogroup.ID, fdrStoreObj);

				for (auto &info : infogroup.InfoList)
				{
					// Save link to the parent
					info.parent_infogroup = &infogroup;

					// Create a record object
					Record *resource = new Record(profile, section, component, fdrStoreObj, infogroup, info);

					// Append it to the list
					RecList.push_back(resource);

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

				// std::cout << "CreateRecords: fdrStoreObj.use_count: "
				// 		  << fdrStoreObj.use_count() << std::endl;
			}
		}
	}
}

// This method will creates a snapshot of all Inventory.log, config.log and Versions.log
// of all the inventory only for the very first time when fdr booted
void FlightDataRecorder_c::CollectAndArchieveBirthCertificate(void)
{
	// 1. execute all the records irrespective of whether birth certificate got created or not
	RefreshAndRecord();

	// 2. create the birth certificate archieve, if not already present
	if (!(std::filesystem::exists(birthCertFilePath)))
	{
		std::string fdrDumpPath = profile.GeneralConfig.LogsBasePath;
		std::string commandStr = "find " + fdrDumpPath + " | grep -e Inventory.log -e Config.log -e Versions.log | xargs tar -cJf " + birthCertFilePath;

		// std::cout << "tarCmd: " << commandStr << std::endl;
		CommandResult_t cmdResult = exec(commandStr.c_str());
		if (cmdResult.cmdExitstatus != FDR_SUCCESS)
		{
			std::cout << "tarCmd command Failed: " << commandStr << std::endl;
		}
		std::cout << "Successfully created Birth certificate: " << birthCertFilePath << std::endl;
	}
	else
	{
		// std::cout << "exist: " << birthCertFilePath << "..so no need to create Birthcertificate again!!" << std::endl;
	}

	// 3. after creating the birth certificate, remove only the "BootEvent" records.
	//    This records needs to be executed only once when FDR comes up. This records 
	//    need not be executed during the FDR runtime.
	// std::cout << "Before bootevent deleting----------------------------------" << std::endl;
	// std::cout << "size of RecList:" << RecList.size() << std::endl;
	DeleteSpecificRecords("AfterBootDelete");
	// std::cout << "after bootevent deleting-----------------------------------" << std::endl;
	// std::cout << "size of RecList:" << RecList.size() << std::endl;
}

void FlightDataRecorder_c::ReadOldRecords(void)
{
	for (auto &rec : RecList)
	{
		if (rec->info.FetchPolicy.compare("Periodic") == 0)
		{
			rec->Load();
		}
	}
}

void FlightDataRecorder_c::RefreshAndRecord(void)
{
	for (auto &rec : RecList)
	{
		bool expt = false;
		try {
			rec->Refresh();
			rec->Store();
		} catch (const std::exception& e) {
			expt = true;
			this->log->warn("RefreshAndRecord(): {}", e.what());
		} catch (...) {
			expt = true;
			this->log->warn("RefreshAndRecord(): unknown exception !!!");
		}

		if (expt) {
			this->CheckExceptionRateLimit();
		}
	}
}

// create a hidden empty file /tmp/.fdrHmcAlive, if it not exist. This file will be used
// to determine whether this fdr instance is due to HMC boot or fdr instance restart.
// In case of HMC reboot: 
// 		Since HMC is an Embedded system, any content written to /tmp directory
//      will be erased after reboot, as it have only ramfs and not have backing disk.
// In case of fdr restart:
// 		* [FDR is a one of the process or service in HMC]
// 		* create /tmp/.fdrHmcAlive hidden file if it does not exists
// 		* Since its only a service in HMC, restart of HMC wont erase the content of /tmp
// 		* with this infra, fdr will identify itself, whether the restart of fdr is due
// 		  to HMC reboot or fdr restart[due to any malfunction or crash or config change, etc]
// 		* This way of identifying the fdr instance restart is required to get the correct
// 		  boot counter maintained as per the BootEventLog.log file
// NOTE:
// 1. This way of identifying the fdr restart will work fine only in HMC[as /tmp directory
// 	  content is cleared on every reboot of HMC]
// 2. This wont work in Host, where /tmp is backed with actual disk and the /tmp directory
// 	  content wont be cleared on every Host reboot.
// 3. This function should be called only after "OnBoot records execution".
void FlightDataRecorder_c::CreateFdrHmcAlive(void)
{
	if (!(std::filesystem::exists(fdrHmcAlivePathName))) {
		std::fstream file; // object of fstream class

		// create the file
		file.open(fdrHmcAlivePathName, std::ios::out);

		//If file is not created, return error
		if (!file) { 
			std::cout << fdrHmcAlivePathName << " :Error in file creation!" << std::endl;
			// no need to abort/exit FDR instance as it wont create a major functionality
			// break in normal function of fdr itself. Just continue the operation of FDR
			// with a error message in the fdr log.
		} else {
			// File is created and close it
			file.close();
		}
	} else {
		std::cout << fdrHmcAlivePathName << " already exist!!" << std::endl;
	}
}

// update the global variable:
//    1. bootCounter: global variable will be updated only once in the FDR init time 
//    2. sensorDirTimestamp: global variable will be updated during both FDR init time and during
//                           every CompactionWindowSecs expiry
// dependency: This function should get called before calling CreateFdrHmcAlive(), so that the
// /usr/bin/FdrBootCounter.sh script could detect the presence of /tmp/.fdrHmcAlive and get the
// correct the value of bootcounter.
void FlightDataRecorder_c::UpdateGlobVariables(bool needtoUpdateBootcounter)
{
	// bootCounter global variable will be updated only once in the FDR init time 
	if (needtoUpdateBootcounter == true) {
		std::string bootcounterDir = profile.GeneralConfig.LogsBasePath + "/" + CommonFdrKeepersDirName + "/";
		std::string bootCountCmd = "/usr/bin/FdrBootCounter.sh " + bootcounterDir;

		// 1. get the Boot Counter
		CommandResult_t bootCountCmdResult = exec(bootCountCmd.c_str());
		if (bootCountCmdResult.cmdExitstatus != FDR_SUCCESS) {
			// if any of the command failed, then this is not the PPF file for this platform
			std::cout << "UpdateGlobVariables: command Failed: " << bootCountCmd << std::endl;
			std::cout << "UpdateGlobVariables: commandresult: "
					<< "cmdExitstatus: " << bootCountCmdResult.cmdExitstatus
					<< "; cmdOutput: " << bootCountCmdResult.cmdOutput << std::endl;
		}
		// 2. check if the file exists
		std::string BootCountFilepath = bootcounterDir + "BootCount.txt";
		if (!(std::filesystem::exists(BootCountFilepath))) {
			std::cout << "BootCount file Not Exist!!: " << BootCountFilepath << std::endl;
			// if the file not exist, then hardcode fixed value to the variable
			bootCounter = "0";
		} else {
			// 3. get the boot counter from the BootCount.txt
			std::ifstream f(BootCountFilepath);
			f >> bootCounter;
			std::cout << "UpdateGlobVariables: bootCounter: " << bootCounter << std::endl;
		}
	}

	// 4. get the current time stamp and convert into string
    std::time_t currentTime = std::time(nullptr);
	std::stringstream ss;
	ss << currentTime;
	sensorDirTimestamp = ss.str();
}

void FlightDataRecorder_c::DeleteSpecificRecords(std::string recRetentionPolicy)
{
	// for Debugging:
	// std::cout << "Before deleting: " << recRetentionPolicy << "--------------" << std::endl;
	// std::cout << "size of RecList:" << RecList.size() << std::endl;
	// for (auto recIt =  RecList.begin(); recIt !=  RecList.end(); ++recIt) { 
	// 	(*recIt)->Print();
	// } 

	// delete the sensor records alone and again create them
	for (auto recIt = RecList.begin(); recIt != RecList.end(); )
	{
		if ((*recIt)->infogroup.RecordRetentionPolicy != recRetentionPolicy) {
			// std::cout << "skip deleting: " << (*recIt)->infogroup.ID
			// 		  << "; CompactionMethod: " << (*recIt)->infogroup.CompactionMethod
			//		  << "; RecordRetentionPolicy: " << (*recIt)->infogroup.RecordRetentionPolicy
			// 		  << std::endl;
			++recIt;
			continue;
		}

		// delete the record [this will trigger the descructor of the respective object]
		delete (*recIt);

		// then delete the entry from the RecList vector
		RecList.erase(recIt);

		// after deleting the element from the vector, reinitalize the iterator from the begining
		recIt = RecList.begin();
	}
	// for Debugging:
	// std::cout << "after deleting-------------------------------------------------------------------------" << std::endl;
	// std::cout << "size of RecList:" << RecList.size() << std::endl;
	// std::cout << "-------------------------------------------------------------------------" << std::endl;
}

// This method will loops throug the entire FDR profile and if it finds the infogroup name
// matching the specified recRetentionPolicy[even if it matches the part of the name], then create
// the record and push it to the RecList. This method will be called at every CompactionWindowSecs expiry.
void FlightDataRecorder_c::CreateSpecificRecords(std::string recRetentionPolicy)
{
	for (auto &section : profile.Sections)
	{
		section.parent_profile = &profile;
		for (auto &component : section.Components)
		{
			component.parent_section = &section;
			for (auto &infogroup : component.InfoGroups)
			{
				
				if (infogroup.RecordRetentionPolicy != recRetentionPolicy) {
					continue;
				}
				infogroup.parent_component = &component;

				// create the FDRStore[samples storage file] here itself and
				// use the same FDRStore pointer for all the records on this infogroup
			    std::shared_ptr<FDRStore> fdrStoreObj;
			    CreateSamplesWriter(profile, section.ID, component.ID, infogroup.ID, fdrStoreObj);

				for (auto &info : infogroup.InfoList)
				{
					// Save link to the parent
					info.parent_infogroup = &infogroup;

					// Create a record object
					Record *resource = new Record(profile, section, component, fdrStoreObj, infogroup, info);

					// Append it to the list
					RecList.push_back(resource);
				}
				// std::cout << "CreateSpecificRecords: fdrStoreObj.use_count: "
				// 		  << fdrStoreObj.use_count() << std::endl;
			}
		}
	}
}

FlightDataRecorder_c::~FlightDataRecorder_c()
{
	if (this->rfc)
	{
		delete this->rfc;
	}

	PPFSanity *fds = SanityChecker.release();
	delete fds;
}