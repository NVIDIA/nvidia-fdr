
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <ctime>

#include "yaml-cpp/yaml.h"	 //yaml-cpp lib used for parsing input (yaml) files
#include <nlohmann/json.hpp> //nlohmann lib used to output data (json) to logfiles

#include "fdr_profile.hpp"
#include "fdr_record.hpp"

#include <filesystem>
#include <boost/algorithm/string.hpp>

using nlohmann::json;

class PlatformProfile_c
{
private:
	/* data */
public:
	Profile_t profile;
	PlatformProfile_c(const char *filename);
	~PlatformProfile_c();
	void Process(void);
};

PlatformProfile_c::PlatformProfile_c(const char *filename)
{
	YAML::Node PlatformProfile = YAML::LoadFile(filename);

	// profile = PlatformProfile.as<Profile_t>;
	profile.GeneralConfig = PlatformProfile["GeneralConfig"].as<GeneralConfig_t>();
	profile.Sections = PlatformProfile["Sections"].as<std::vector<Section_t>>();
}

// TODO: find own implementation
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
	return result;
}

void to_json(json &j, const fdr_record &rec)
{
	j = json{{"TimeStamp", rec.TimeStamp}, {"InfoName", rec.InfoName}, {"InfoValue", rec.InfoValue}};
}

void PlatformProfile_c::Process(void)
{
	for (auto &section : profile.Sections)
	{
		// std::cout << "Section.ID: " << section.ID << "\n";
		for (auto &component : section.Components)
		{
			// std::cout << "\tComponent.ID: " << component.ID << "\n";
			for (auto &infogroup : component.InfoGroups)
			{
				for (auto &info : infogroup.InfoList)
				{
					// std::cout << "\tID: " << info.ID << "\n";
					if (difftime(std::time(nullptr), info.LastUpdateAt) >= info.FetchFreqSecs)
					{
						std::string logdir = profile.GeneralConfig.LogsBasePath + "/" + section.ID + "/" + component.ID + "/";
						std::string logfile = infogroup.ID + ".log";

						// Search & replace all params with values in the commands/paths
						for (auto &param : component.Params)
						{
							//$param.name --> param.value
							boost::replace_all(info.FetchPath, "$" + param.name, param.value);
						}

						// Create log directory if missing
						std::filesystem::path dir(logdir);
						if (!(std::filesystem::exists(dir)))
						{
							if (!(std::filesystem::create_directories(dir)))
								std::cout << "Failed to create directory: " << dir << std::endl;
							// TODO: error handling
						}

						// Open logfile
						std::ofstream myfile(logdir + logfile, std::ios_base::app);
						if (myfile.is_open())
						{
							fdr_record record;
							std::time_t current_time = std::time(nullptr);

							record.TimeStamp = std::to_string(current_time);
							record.InfoName = info.ID;
							record.InfoValue = exec(info.FetchPath.c_str());
							info.LastUpdateAt = current_time;

							json jrecord{record};		 // Marshall record to json
							myfile << jrecord[0].dump(); // NOTE: for some reason, the json marshalling above produces an array instead of a single element
							// myfile << jrecord[0].dump(4);
							myfile << std::endl;

							myfile.close();
						}
						// TODO: error handling
					}
				}
			}
		}
	}
}

PlatformProfile_c::~PlatformProfile_c()
{
}

int main(void)
{

#if 0
	PlatformProfile_c platform("fdr_vulcan.yaml");
#else
	exec("cat fdr_vulcan.yaml | yaml2json - > fdr_vulcan.json"); // Convert yaml to json(which is still yaml) to resolve all internal references (anchors and aliases)
	PlatformProfile_c platform("fdr_vulcan.json");
#endif

	while (true)
	{
		platform.Process();
		sleep(1);
	}

	return EXIT_SUCCESS;
}

#if 0
std::cout << __func__ << ":" << __LINE__ << std::endl;
#endif