
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <ctime>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <exception>

#include "yaml-cpp/yaml.h" //yaml-cpp lib used for parsing input (yaml) files

#include "fdr_common.hpp"
#include "fdr_policy.hpp"
#include "fdr_record.hpp"
#include "fdr_store.hpp"

#include <filesystem>
#include <boost/algorithm/string.hpp>
#include <systemd/sd-bus.h>

class FlightDataRecorder_c
{
private:
	/* data */
	std::vector<Record *> RecList;

	int FindAndLoadPlatformProfile(void);
	int ConvertPPFToStruct(const std::string filename);
	int ExecuteFingerPrintRules(void);

public:
	Profile_t profile;
	FlightDataRecorder_c(const std::string filename);
	~FlightDataRecorder_c();
	void CreateRecords(void);
	void ReadOldRecords(void);
	void RefreshAndRecord(void);
	void Compactor(void);
};

CommandResult_t exec(const char *cmd)
{
	int exitcode = 0;
	std::array<char, ONE_MB> buffer {};
	std::string result;

	FILE *pipe = popen(cmd, "r");
	if (pipe == nullptr) {
		throw std::runtime_error("popen() failed!");
	}
	try {
		std::size_t bytesread;
		while ((bytesread = std::fread(buffer.data(), sizeof(buffer.at(0)), sizeof(buffer), pipe)) != 0) {
			result += std::string(buffer.data(), bytesread);
		}
	} catch (...) {
		pclose(pipe);
		throw;
	}
	exitcode = WEXITSTATUS(pclose(pipe));

	return CommandResult_t {result, exitcode};
}

FlightDataRecorder_c::FlightDataRecorder_c(const std::string filename)
{
	int retVal;

	(void)filename;

	// have to identify the platform
	retVal = FindAndLoadPlatformProfile();
	if (retVal != FDR_SUCCESS) {
		std::cout << "Not able to find the right PPF file for this platform..Exiting!!" << std::endl;
		exit(EXIT_FAILURE);
	}

	// create the records from the PPF file
	CreateRecords();
}

int FlightDataRecorder_c::FindAndLoadPlatformProfile(void)
{
    // Path to the directory
	std::vector <std::string> SupportedPlatforms;
	struct stat sb;
	int retVal;
  
	std::string SupportedPlatformsDir = "./platforms"; //Most relevant path if/when a developer is running fdr from source directory
	if (!(std::filesystem::exists("./platforms"))) {
		SupportedPlatformsDir = "/mnt/source/fdr/platform"; //Applicable when FDR is running from a installed location
	}

    // step 1: get all the PPF files
    for (const auto& entry : std::filesystem::directory_iterator(SupportedPlatformsDir)) {
        // Testing whether the path points to a non-directory or not If it does, displays path
        if (stat(entry.path().c_str(), &sb) == 0 && !(sb.st_mode & S_IFDIR)) {
			SupportedPlatforms.push_back(entry.path());
		}
    }

	// for (int i=0; i<SupportedPlatforms.size(); i++) {
	// 	std::cout << i << ": before sorting: SupportedPlatforms: " << SupportedPlatforms[i] << std::endl;
	// }

	// sort the list of platform files ascendingly
	sort(SupportedPlatforms.begin(), SupportedPlatforms.end());

	// step 2: loop through the list of PPFs, execute the rules and find the right PPF
	for (auto filename : SupportedPlatforms) {
		
		std::cout << "Trying " + filename << std::endl;
		
		std::string filetoload = filename;

		//HACK: If YAML, convert to JSON because yaml-cpp has trouble parsing yaml with anchors and aliases
		if (filename.substr(filename.find_last_of(".")) == ".yaml")
		{
			filetoload = "/tmp/fdr_ppf_temp.json";
			std::string yamltojson = "yaml2json " + filename + " > " + filetoload; // eg: yaml2json fdr_ppf_vulcan.yaml > /tmp/fdr_ppf_temp.json
			exec(yamltojson.c_str());
		}

		// convert the given PPF to data structs
		retVal = ConvertPPFToStruct(filetoload);
		if (retVal != FDR_SUCCESS) {
			std::cout << "ExecuteFingerPrintRules failed...Try another" << std::endl;
			continue;
		}
		// execute the Fingerprint in the PPF
		retVal = ExecuteFingerPrintRules();
		if (retVal == FDR_SUCCESS) {
			std::cout << "Found the PPF file: " << filetoload << std::endl;
			// now Data struct have all the values from this PPF file. Hence return FDR_SUCCESS.
			return FDR_SUCCESS;
		}
	}

	return FDR_ERR_GENFAILURE;
}

// convert the Platform Profile File[PPF] file and store the values in the "profile" data struct
int FlightDataRecorder_c::ConvertPPFToStruct(const std::string filename)
{
	// catch any exception while parsing through the yaml or json file
	try {
	#if 1
		YAML::Node PlatformProfile = YAML::LoadFile(filename);
	#else
		// HACK: yaml-cpp lib seems to have trouble parsing yaml with anchors and aliases, so need to convert to json first
		std::string yamltojson = "cat " + filename + " | yaml2json - > " + filename + ".json"; // eg: cat fdr_vulcan.yaml | yaml2json - > fdr_vulcan.yaml.json
		exec(yamltojson.c_str());
		YAML::Node PlatformProfile = YAML::LoadFile(filename + ".json"); // NOTE: remember, json is a subset of yaml, so we can still use YAML::LoadFile to load it
	#endif

		profile.FingerPrint = PlatformProfile["FingerPrint"].as<FingerPrint_t>();
		profile.GeneralConfig = PlatformProfile["GeneralConfig"].as<GeneralConfig_t>();
		profile.Sections = PlatformProfile["Sections"].as<std::vector<Section_t>>();

		return EXIT_SUCCESS;
	} catch(std::exception& e) {
		std::cout << "Exception while parsing " << filename << ": " << e.what() << std::endl;
		return EXIT_FAILURE;
	}
}

// use the values from "profile" data struct and execute the rules
int FlightDataRecorder_c::ExecuteFingerPrintRules(void)
{
	for (auto CheckRule : profile.FingerPrint.Checks) {
        CommandResult_t cmdResult = exec(CheckRule.c_str());
		// std::cout << "\tcommandresult: "
		// 		  << "cmdExitstatus: " << cmdResult.cmdExitstatus << std::endl;
				//   << "; cmdOutput: " << cmdResult.cmdOutput << std::endl;
		if (cmdResult.cmdExitstatus == FDR_ERR_GENFAILURE) {
			// if any of the command failed, then this is not the PPF file for this platform
			std::cout << "command Failed: " << CheckRule << std::endl;
			return FDR_ERR_GENFAILURE;
		}
	}
	return FDR_SUCCESS;
}

void FlightDataRecorder_c::CreateRecords(void)
{
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
				for (auto &info : infogroup.InfoList)
				{
					// Save link to the parent
					info.parent_infogroup = &infogroup;

					// Create a record object
					Record *resource = new Record(profile, section, component, infogroup, info);

					// Append it to the list
					RecList.push_back(resource);
				}
			}
		}
	}
}

void FlightDataRecorder_c::ReadOldRecords(void)
{
	for (auto &rec1 : RecList)
	{
		rec1->Load();
	}
}

void FlightDataRecorder_c::RefreshAndRecord(void)
{
	for (auto &rec : RecList)
	{
		rec->Refresh();
		rec->Store();
	}
}


void FlightDataRecorder_c::Compactor(void)
{
	for (auto &section : profile.Sections)
	{
		for (auto &component : section.Components)
		{
			for (auto &infogroup : component.InfoGroups)
			{
				if (infogroup.CompactionMethod != "Average")
					continue; // Only numerical stats can be compacted not text etc. for now

				double timeSinceLastCompaction = difftime(std::time(nullptr), infogroup.LastCompactedAt);
				// std::cout << "LastCompactedAt: " << infogroup.LastCompactedAt 
				// 		  << "\tCurrentTime: " << std::time(nullptr) 
				// 		  << "\tCompactionFreqSecs: " << infogroup.CompactionFreqSecs
				// 		  << "\ttimeSinceLastCompaction: " << timeSinceLastCompaction
				// 		  << "\tCompactionPolicy: " << infogroup.CompactionPolicy
				// 		  << std::endl;

				// Skip if its not time to compact yet
				if ((infogroup.CompactionPolicy == "Periodic") && (timeSinceLastCompaction < infogroup.CompactionFreqSecs)) {
					// std::cout << "Skippping Compaction" << std::endl;
					continue;
				}else{
					// std::cout << "Proceeding with Compaction" << std::endl;
				}

				if (profile.GeneralConfig.LogsFormat == ENCODING_CHOICE_JSON || profile.GeneralConfig.LogsFormat == ENCODING_CHOICE_BINARY){
					std::string logdir = profile.GeneralConfig.LogsBasePath + "/" + section.ID + "/" + component.ID + "/";
					std::string logfile = infogroup.ID + ".log";
					std::string statsfile = infogroup.ID + ".stats";

					// Create a map info.ID --> {n,min,max,sum,}
					std::map<std::string, fdr::fdr_stat> stats_so_far;

					FDRStore fdrlogs(logdir + logfile, profile.GeneralConfig.LogsFormat);
					fdr::fdr_sample readrec;
					while (fdrlogs.readnext(&readrec))
					{
						stats_so_far[readrec.paramname()].set_paramname(readrec.paramname());
						stats_so_far[readrec.paramname()].set_numsamples(stats_so_far[readrec.paramname()].numsamples() + 1);
						stats_so_far[readrec.paramname()].set_avg(stats_so_far[readrec.paramname()].avg() + readrec.paramvalueint64()); // TODO: using avg field as sum. avoid overflow.
						if (stats_so_far[readrec.paramname()].min() != 0)
							stats_so_far[readrec.paramname()].set_min(std::min(stats_so_far[readrec.paramname()].min(), readrec.paramvalueint64()));
						else
							stats_so_far[readrec.paramname()].set_min(readrec.paramvalueint64());
						stats_so_far[readrec.paramname()].set_max(std::max(stats_so_far[readrec.paramname()].max(), readrec.paramvalueint64()));
						stats_so_far[readrec.paramname()].set_fromtime(stats_so_far[readrec.paramname()].fromtime() == 0 ? readrec.timestamp() : stats_so_far[readrec.paramname()].fromtime());
						stats_so_far[readrec.paramname()].set_totime(readrec.timestamp());
					}
					for (auto &stat : stats_so_far)
					{
						stat.second.set_avg(stat.second.avg() / stat.second.numsamples());

						std::cout << "-----Stats for ID: " << stat.first << std::endl;
						std::cout << "Num: " << stat.second.numsamples() << std::endl;
						std::cout << "Min: " << stat.second.min() << std::endl;
						std::cout << "Max: " << stat.second.max() << std::endl;
						std::cout << "Avg: " << stat.second.avg() << std::endl;

						FDRStore fdrstats(logdir + statsfile, profile.GeneralConfig.LogsFormat);
						fdrstats.append(stat.second);
						infogroup.LastCompactedAt = std::time(nullptr);
						// Delete the records we just compacted
						std::string filetodelete = logdir + logfile;
						if (remove(filetodelete.c_str()) != 0)
						{
							perror("Error deleting file");
							std::cout << "Failed to delete: " << filetodelete << std::endl;
						}
						else
						{
							std::cout << "Succesfully deleted: " << filetodelete << std::endl;
						}
					}

				}
				else if (profile.GeneralConfig.LogsFormat == ENCODING_CHOICE_DB){
					std::string logdir = profile.GeneralConfig.LogsBasePath + "/";
					std::string logfile = profile.GeneralConfig.DatabaseName;

					// Create a map info.ID --> {n,min,max,sum,}
					std::map<std::string, fdr_stat_sql> stats_so_far;

					FDRStore fdrlogs(logdir + logfile, profile.GeneralConfig.LogsFormat, infogroup.ID, section.ID, component.ID);
					fdr_sample_sql readrec;
					while (fdrlogs.readnext(&readrec))
					{
						stats_so_far[readrec.paramName].paramName = readrec.paramName;
						stats_so_far[readrec.paramName].numsamples += 1;
						stats_so_far[readrec.paramName].avg += readrec.paramValueInt64; // TODO: using avg field as sum. avoid overflow.
						if (stats_so_far[readrec.paramName].min != 0)
							stats_so_far[readrec.paramName].min = std::min(stats_so_far[readrec.paramName].min, readrec.paramValueInt64);
						else
							stats_so_far[readrec.paramName].min = readrec.paramValueInt64;
						stats_so_far[readrec.paramName].max = std::max(stats_so_far[readrec.paramName].max, readrec.paramValueInt64);
						stats_so_far[readrec.paramName].fromtime = stats_so_far[readrec.paramName].fromtime == 0 ? readrec.timestamp : stats_so_far[readrec.paramName].fromtime;
						stats_so_far[readrec.paramName].totime = readrec.timestamp;
					}

					FDRStore fdrstats(logdir + logfile, profile.GeneralConfig.LogsFormat, infogroup.ID, section.ID, component.ID);
					fdrstats.createStatesTable();

					for (auto &stat : stats_so_far)
					{
						stat.second.avg = stat.second.avg / stat.second.numsamples;

						std::cout << "-----Stats for ID: " << stat.first << std::endl;
						std::cout << "Num: " << stat.second.numsamples << std::endl;
						std::cout << "Min: " << stat.second.min << std::endl;
						std::cout << "Max: " << stat.second.max << std::endl;
						std::cout << "Avg: " << stat.second.avg << std::endl;

						fdrstats.appendStat(stat.second);
						infogroup.LastCompactedAt = std::time(nullptr);
					}
					//Delete existing records
					fdrlogs.deleteRecords();
				}
			}
		}
	}
}

FlightDataRecorder_c::~FlightDataRecorder_c()
{
}

static inline const char *strna(const char *s)
{
	return s ? s : "n/a";
}

sd_bus *bus = NULL;

int message_callback(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
	(void)userdata;
	(void)ret_error;

	printf("callback: path=%s interface=%s member=%s\n",
		   strna(sd_bus_message_get_path(m)),
		   strna(sd_bus_message_get_interface(m)),
		   strna(sd_bus_message_get_member(m)));

	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	int r;

	r = sd_bus_get_property(bus, "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager/Devices/1",
							"org.freedesktop.NetworkManager.Device.Statistics", "RxBytes",
							&error, &reply, "t");
	if (r < 0)
	{
		printf("sd_bus_get_property failed: error=%s\n", error.message);
	}

	uint64_t rxbytes;
	r = sd_bus_message_read(reply, "t", &rxbytes);
	if (r < 0)
		printf("sd_bus_message_read failed\n");

	printf("rxbytes =%" PRIu64 "\n", rxbytes);
	// sd_bus_message_dump(reply, stdout, SD_BUS_MESSAGE_DUMP_SUBTREE_ONLY);

	return 0;
}

int main(void)
{
	// GOOGLE_PROTOBUF_VERIFY_VERSION;//Ensure protobuf header and library are compatible.

	sd_bus_default_system(&bus);

	FlightDataRecorder_c fdr("fdr_vulcan.yaml");

	// Read in the last recorded values from log files
	fdr.ReadOldRecords();

#if 0
	// Install Listeners so we can avoid polling as much as possible
	sd_bus_match_signal(
		bus,												// bus
		NULL,												// ret
		NULL,												// sender
		"/org/freedesktop/NetworkManager/Devices/1",		// path
		"org.freedesktop.NetworkManager.Device.Statistics", // interface
		"PropertiesChanged",								// member
		message_callback,									// callback
		NULL);												// userdata

	while (1)
	{
		sd_bus_wait(bus, UINT64_MAX);
		while (sd_bus_process(bus, NULL))
		{
		}
	}
#endif

	while (true)
	{
		// Start the core engine of fetching and recording
		fdr.RefreshAndRecord();

		// Compactor
		fdr.Compactor();

		sleep(1);
	}

	sd_bus_unref(bus);
	return EXIT_SUCCESS;
}

#if 0
std::cout << __func__ << ":" << __LINE__ << std::endl;
#endif
