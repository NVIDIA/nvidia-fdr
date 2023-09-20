/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#pragma once

#include <vector>
#include <ctime>
#include <yaml-cpp/yaml.h>

struct Profile_t;
struct GeneralConfig_t;
struct Section_t;
struct Component_t;
struct InfoGroup_t;
struct Info_t;

struct FingerPrint_t
{
	std::vector<std::string> Checks; // list of checks to find the platform
};

struct Preconditions_t
{
	size_t Threshold; // Time threshold for precondition check
	std::vector<std::string> Checks; // Platform preconditions list of checks
};

// Params for storing AML events configuration on OpenBMC platforms
struct Events_t {
    std::string objectPath;
    std::string interface;
    std::string member;
};

struct GeneralConfig_t
{
	std::string LogsBasePath; // Root directory for all logs for this system
	std::string LogsFormat;	  // Data encoding format to use (JSON|Binary etc.)
	std::string DatabaseName; // Database name to be used

	size_t LoggingFileMaxSize;   // Rotate the logging file once it exceed the size
	size_t LoggingFileNumber;    // How many logging files to keep
	std::string LoggingLevel;    // default log level, possible values are: trace, debug, info, warn, err, critical, off

	std::string RedfishSchema;
	std::string RedfishUser;
	std::string RedfishPassword;

	uint64_t CompactionWindowSecs;
	uint64_t CompactionSubWindowSecs;
	uint64_t HiFiDataPreserveTimeSecs;

	int64_t ExceptionAllowNumber;
	double ExceptionAllowRate;

	Events_t eventParams; // Events params defined under GeneralConfig
};

struct CommandParams_t
{
	std::string Command;	// Command (executable path that bash can resolve)
	std::string WorkingDir; // Directory in which to run the command (optional)
};
struct DbusParams_t
{
	std::string Service;
	std::string ObjectPath;
	std::string Interface;
	std::string Property;
	int64_t DevId;
	int Opcode;
	int Arg1;
	int Arg2;

};

struct RedfishParams_t
{
	std::string URI;
	std::string JSONPointer;
};

struct Info_t
{
	std::string ID;				   // Unique ID for this information piece
	unsigned int ParamID;		   // Unique ID to be used instead of ParamName when needed
	std::string FetchPolicy;	   // Periodic|OnEvent
	std::string FetchMethod;	   // Fetch method (Command|File|DBUS|GPIO)
	std::string StorePolicy;	   // Storage policy (EveryFetch|OnChange|etc.)
	int FetchFreqSecs;			   // Seconds between each fetch
	int StoreFreqSecs;			   // Seconds between each store (if StorePolicy==Periodic)
	// TODO: make these Params a Union to save memory
	CommandParams_t CommandParams; // If FetchMethod==Command
	DbusParams_t DbusParams;	   // If FetchMethod==DBUS
	RedfishParams_t RedfishParams; // If FetchMethod==Redfish
	std::string DataType;		   // Data Type ()
	InfoGroup_t *parent_infogroup; // Pointer to the infogroup this info belongs to
	std::string FetchType;
};

struct Param_t
{
	std::string name;  // Name of the param
	std::string value; // Value for this param. Note: numbers are represented as strings as well
};

struct InfoGroup_t
{
	std::string ID;				   // Unique ID for this group eg: Inventory|Config|Versions|Errors|Stats
	std::vector<Info_t> InfoList;  // List of information related to this component
	std::string RecordRetentionPolicy;  // Compaction policy (Periodic|OnChange|etc.)
	std::string CompactionMethod;  // Compaction method (Average|Discard|etc.)
	int CompactionFreqSecs;		   // Seconds between each compaction (if RecordRetentionPolicy==Periodic)
	Component_t *parent_component; // Pointer to the component this infogroup belongs to
	std::time_t LastCompactedAt;   // Time of last compaction
};

struct Component_t
{
	std::string ID;						 // Unique ID for this part of the platform
	std::vector<Param_t> Params;		 // List of parameter & value pairs relevant for this component
	std::vector<InfoGroup_t> InfoGroups; // List of information Groups related to this component
	Section_t *parent_section;			 // Pointer to the section this component belongs to
};

struct Section_t
{
	std::string ID;						 // Unique ID for this part of the platform
	std::vector<Component_t> Components; // List of components under this section
	Profile_t *parent_profile;			 // Pointer to the profile this section belongs to
};

struct Profile_t
{
	FingerPrint_t FingerPrint;
	Preconditions_t Preconditions;
	GeneralConfig_t GeneralConfig;	 // General config for this system
	std::vector<Section_t> Sections; // List of Component Categories/Sections in the platform
};

// TODO: Find way to move following functions to a cpp file and not a header
namespace YAML
{
	template <>
	struct convert<FingerPrint_t>
	{
		static bool decode(const Node &node, FingerPrint_t &rhs)
		{
			if (node["Checks"])
			{
				rhs.Checks = node["Checks"].as<std::vector<std::string>>();
			}

			return true;
		}
	};

	template <>
	struct convert<Preconditions_t>
	{
		static bool decode(const Node &node, Preconditions_t &rhs)
		{
			rhs.Threshold = node["Threshold"] ? node["Threshold"].as<size_t>() : 60; // default 1min
			if (node["Checks"])
			{
				rhs.Checks = node["Checks"].as<std::vector<std::string>>();
			}

			return true;
		}
	};

	template <>
	struct convert<Events_t>
	{
		static bool decode(const Node &node, Events_t &rhs)
		{
			if (node["ObjectPath"])
			{
				rhs.objectPath = node["ObjectPath"].as<std::string>();
			}
			if (node["Interface"])
			{
				rhs.interface = node["Interface"].as<std::string>();
			}
			if (node["Member"])
			{
				rhs.member = node["Member"].as<std::string>();
			}

			return true;
		}
	};

	template <>
	struct convert<GeneralConfig_t>
	{
#if 0
		static Node encode(const GeneralConfig_t &rhs)
		{
			Node node;
			node.push_back(rhs.x);
			node.push_back(rhs.y);
			node.push_back(rhs.z);
			return node;
		}
#endif
		static bool decode(const Node &node, GeneralConfig_t &rhs)
		{
			// just take the first element[of type std::string] from the vector
			rhs.LogsBasePath = node["LogsBasePath"].as<std::vector<std::string>>().front();
			rhs.LogsFormat = node["LogsFormat"].as<std::string>();
			if (node["DatabaseName"] || rhs.LogsFormat == "DB")
				rhs.DatabaseName = node["DatabaseName"].as<std::string>();

			rhs.LoggingFileMaxSize = node["LoggingFileMaxSize"] ? node["LoggingFileMaxSize"].as<size_t>() : 1048576; // default 1 MB
			rhs.LoggingFileNumber = node["LoggingFileNumber"] ? node["LoggingFileNumber"].as<size_t>() : 3; // default 3 log files
			rhs.LoggingLevel = node["LoggingLevel"] ? node["LoggingLevel"].as<std::string>() : std::string{"info"}; // default log level

			rhs.RedfishSchema = node["RedfishSchema"] ? node["RedfishSchema"].as<std::string>() : std::string{};
			rhs.RedfishUser = node["RedfishUser"] ? node["RedfishUser"].as<std::string>() : std::string{};
			rhs.RedfishPassword = node["RedfishPassword"] ? node["RedfishPassword"].as<std::string>() : std::string{};
			rhs.CompactionWindowSecs = node["CompactionWindowSecs"] ? node["CompactionWindowSecs"].as<uint64_t>() : 0;
			rhs.CompactionSubWindowSecs = node["CompactionSubWindowSecs"] ? node["CompactionSubWindowSecs"].as<uint64_t>() : 0;
			rhs.HiFiDataPreserveTimeSecs = node["HiFiDataPreserveTimeSecs"] ? node["HiFiDataPreserveTimeSecs"].as<uint64_t>() : 0;

			rhs.ExceptionAllowNumber = node["ExceptionAllowNumber"] ? node["ExceptionAllowNumber"].as<int64_t>() : 128;
			rhs.ExceptionAllowRate = node["ExceptionAllowRate"] ? node["ExceptionAllowRate"].as<float>() : 0.5;

			// Events config will be applicable for only OpenBmc platforms
			if (node["Events"])
			{
				rhs.eventParams = node["Events"].as<Events_t>();
			}

			return true;
		}
	};

	template <>
	struct convert<CommandParams_t>
	{
		static bool decode(const Node &node, CommandParams_t &rhs)
		{
			if (node["Command"])
			{
				rhs.Command = node["Command"].as<std::string>();
			}
			if (node["WorkingDir"])
			{
				rhs.WorkingDir = node["WorkingDir"].as<std::string>();
			}

			return true;
		}
	};

	template <>
	struct convert<DbusParams_t>
	{
		static bool decode(const Node &node, DbusParams_t &rhs)
		{
			if (node["Service"])
			{
				rhs.Service = node["Service"].as<std::string>();
			}
			if (node["ObjectPath"])
			{
				rhs.ObjectPath = node["ObjectPath"].as<std::string>();
			}
			if (node["Interface"])
			{
				rhs.Interface = node["Interface"].as<std::string>();
			}
			if (node["Property"])
			{
				rhs.Property = node["Property"].as<std::string>();
			}
			if (node["DevId"])
			{
				rhs.DevId = node["DevId"].as<std::uint64_t>();
			}
			if (node["Opcode"])
			{
				rhs.Opcode = node["Opcode"].as<uint8_t>();
			}
			if (node["Arg1"])
			{
				rhs.Arg1 = node["Arg1"].as<uint8_t>();
			}
			if (node["Arg2"])
			{
				rhs.Arg2 = node["Arg2"].as<uint8_t>();
			}

			return true;
		}
	};

	template <>
	struct convert<RedfishParams_t>
	{
		static bool decode(const Node &node, RedfishParams_t &rhs)
		{
			if (node["URI"])
			{
				rhs.URI = node["URI"].as<std::string>();
			}
			if (node["JSONPointer"])
			{
				rhs.JSONPointer = node["JSONPointer"].as<std::string>();
			}

			return true;
		}
	};

	template <>
	struct convert<Info_t>
	{
		static bool decode(const Node &node, Info_t &rhs)
		{
			rhs.ID = node["ID"].as<std::string>();
			rhs.ParamID = node["ParamID"].as<unsigned int>();
			rhs.DataType = node["DataType"].as<std::string>();
			if (node["FetchMethod"])
			{
				rhs.FetchMethod = node["FetchMethod"].as<std::string>();
			}
			if (node["FetchPolicy"])
			{
				rhs.FetchPolicy = node["FetchPolicy"].as<std::string>();
			}
			if (node["StorePolicy"])
			{
				rhs.StorePolicy = node["StorePolicy"].as<std::string>();
			}
			if (node["FetchType"])
			{
				rhs.FetchType = node["FetchType"].as<std::string>();
			}
			if (node["FetchFreqSecs"])
			{
				rhs.FetchFreqSecs = node["FetchFreqSecs"].as<int>();
			}
			if (node["StoreFreqSecs"])
			{
				rhs.StoreFreqSecs = node["StoreFreqSecs"].as<int>();
			}
			if (node["CommandParams"])
			{
				rhs.CommandParams = node["CommandParams"].as<CommandParams_t>();
			}
			if (node["DbusParams"])
			{
				rhs.DbusParams = node["DbusParams"].as<DbusParams_t>();
			}
			if (node["RedfishParams"])
			{
				rhs.RedfishParams = node["RedfishParams"].as<RedfishParams_t>();
			}

			return true;
		}
	};

	template <>
	struct convert<Param_t>
	{
		static bool decode(const Node &node, Param_t &rhs)
		{
			rhs.name = node["name"].as<std::string>();
			try {
				// try to get as std::string
				rhs.value = node["value"].as<std::string>();
			} catch(std::exception& e) {
				// try to get as std::vector<std::string> and get only the first element of the vector
				rhs.value = node["value"].as<std::vector<std::string>>().front();
			}
			return true;
		}
	};

	template <>
	struct convert<InfoGroup_t>
	{
		static bool decode(const Node &node, InfoGroup_t &rhs)
		{
			rhs.ID = node["ID"].as<std::string>();
			rhs.InfoList = node["InfoList"].as<std::vector<Info_t>>();

			if (node["RecordRetentionPolicy"])
			{
				rhs.RecordRetentionPolicy = node["RecordRetentionPolicy"].as<std::string>();
			}
			if (node["CompactionMethod"])
			{
				rhs.CompactionMethod = node["CompactionMethod"].as<std::string>();
			}
			if (node["CompactionFreqSecs"])
			{
				rhs.CompactionFreqSecs = node["CompactionFreqSecs"].as<int>();
			}

			rhs.LastCompactedAt = 0;

			return true;
		}
	};

	template <>
	struct convert<Component_t>
	{
		static bool decode(const Node &node, Component_t &rhs)
		{
			rhs.ID = node["ID"].as<std::string>();
			if (node["Params"])
			{
				rhs.Params = node["Params"].as<std::vector<Param_t>>();
			}
			rhs.InfoGroups = node["InfoGroups"].as<std::vector<InfoGroup_t>>();

			return true;
		}
	};

	template <>
	struct convert<Section_t>
	{
		static bool decode(const Node &node, Section_t &rhs)
		{
			rhs.ID = node["ID"].as<std::string>();
			rhs.Components = node["Components"].as<std::vector<Component_t>>();
			return true;
		}
	};
	template <>
	struct convert<Profile_t>
	{
		static bool decode(const Node &node, Profile_t &rhs)
		{
			rhs.FingerPrint = node["FingerPrint"].as<FingerPrint_t>();
			rhs.Preconditions = node["Preconditions"].as<Preconditions_t>();
			rhs.GeneralConfig = node["GeneralConfig"].as<GeneralConfig_t>();
			rhs.Sections = node["Sections"].as<std::vector<Section_t>>();

			return true;
		}
	};

}
