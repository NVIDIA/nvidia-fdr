
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <ctime>

#include "yaml-cpp/yaml.h" //yaml-cpp lib used for parsing input (yaml) files

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

public:
	Profile_t profile;
	FlightDataRecorder_c(const std::string filename);
	~FlightDataRecorder_c();
	void CreateRecords(void);
	void ReadOldRecords(void);
	void RefreshAndRecord(void);
	void Compactor(void);
};

std::string exec(const char *cmd)
{
	std::array<char, 128> buffer;
	std::string result;
	std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
	if (!pipe)
	{
		throw std::runtime_error("popen() failed!");
	}
	while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
	{
		result += buffer.data();
	}

	//Remove newline from end if applicable
	if (result[result.length()-1] == '\n')
		result.erase(result.length()-1);

	return result;
}

FlightDataRecorder_c::FlightDataRecorder_c(const std::string filename)
{
#if 1
	YAML::Node PlatformProfile = YAML::LoadFile(filename + ".json");
#else
	// HACK: yaml-cpp lib seems to have trouble parsing yaml with anchors and aliases, so need to convert to json first
	std::string yamltojson = "cat " + filename + " | yaml2json - > " + filename + ".json"; // eg: cat fdr_vulcan.yaml | yaml2json - > fdr_vulcan.yaml.json
	exec(yamltojson.c_str());
	YAML::Node PlatformProfile = YAML::LoadFile(filename + ".json"); // NOTE: remember, json is a subset of yaml, so we can still use YAML::LoadFile to load it
#endif

	// profile = PlatformProfile.as<Profile_t>;
	profile.GeneralConfig = PlatformProfile["GeneralConfig"].as<GeneralConfig_t>();
	profile.Sections = PlatformProfile["Sections"].as<std::vector<Section_t>>();

	// Read in the policy file
	CreateRecords();
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

				std::string logdir = profile.GeneralConfig.LogsBasePath + "/";
				std::string logfile = profile.GeneralConfig.DatabaseName;
				std::string statsfile = infogroup.ID + ".stats";

				// Create a map info.ID --> {n,min,max,sum,}
				std::map<std::string, fdr_stat> stats_so_far;

				FDRStore fdrlogs(logdir + logfile, profile.GeneralConfig.LogsFormat, infogroup.ID, section.ID, component.ID);
				fdr_sample readrec;
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

FlightDataRecorder_c::~FlightDataRecorder_c()
{
}

static inline const char *strna(const char *s)
{
	return s ?: "n/a";
}

sd_bus *bus = NULL;

int message_callback(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
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

	printf("rxbytes =%ld\n", rxbytes);
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
