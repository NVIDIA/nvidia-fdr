#pragma once

#include "yaml-cpp/yaml.h" //yaml-cpp lib used for parsing input (yaml) files
#include <vector>
#include <ctime>

struct Profile_t;
struct GeneralConfig_t;
struct Section_t;
struct Component_t;
struct InfoGroup_t;
struct Info_t;

struct GeneralConfig_t
{
	std::string LogsBasePath; // Root directory for all logs for this system
	std::string LogsFormat;	  // Data encoding format to use (JSON|Binary etc.)
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
};
struct Info_t
{
	std::string ID;				   // Unique ID for this information piece
	std::string FetchPolicy;	   // Periodic|OnEvent
	std::string FetchMethod;	   // Fetch method (Command|File|DBUS|GPIO)
	std::string StorePolicy;	   // Storage policy (Periodic|OnChange|etc.)
	int FetchFreqSecs;			   // Seconds between each fetch
	int StoreFreqSecs;			   // Seconds between each store (if StorePolicy==Periodic)
	CommandParams_t CommandParams; // If FetchMethod==Command
	DbusParams_t DbusParams;	   // If FetchMethod==DBUS
	std::string DataType;		   // Data Type ()
	InfoGroup_t *parent_infogroup; // Pointer to the infogroup this info belongs to
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
	std::string CompactionPolicy;  // Compaction policy (Periodic|OnChange|etc.)
	std::string CompactionMethod;  // Compaction method (Average|Discard|etc.)
	int CompactionFreqSecs;		   // Seconds between each compaction (if CompactionPolicy==Periodic)
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
	GeneralConfig_t GeneralConfig;	 // General config for this system
	std::vector<Section_t> Sections; // List of Component Categories/Sections in the platform
};

// TODO: Find way to move following functions to a cpp file and not a header
namespace YAML
{
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
			rhs.LogsBasePath = node["LogsBasePath"].as<std::string>();
			rhs.LogsFormat = node["LogsFormat"].as<std::string>();
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

			return true;
		}
	};

	template <>
	struct convert<Info_t>
	{
		static bool decode(const Node &node, Info_t &rhs)
		{
			rhs.ID = node["ID"].as<std::string>();
			if (node["DataType"])
			{
				rhs.DataType = node["DataType"].as<std::string>();
			}
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

			return true;
		}
	};

	template <>
	struct convert<Param_t>
	{
		static bool decode(const Node &node, Param_t &rhs)
		{
			rhs.name = node["name"].as<std::string>();
			rhs.value = node["value"].as<std::string>();

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

			if (node["CompactionPolicy"])
			{
				rhs.CompactionPolicy = node["CompactionPolicy"].as<std::string>();
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
			rhs.GeneralConfig = node["GeneralConfig"].as<GeneralConfig_t>();
			rhs.Sections = node["Sections"].as<std::vector<Section_t>>();

			return true;
		}
	};

}
