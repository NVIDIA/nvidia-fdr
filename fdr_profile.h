#pragma once

// #include <stdlib.h>
// #include <stdio.h>

typedef struct
{
	std::string LogsBasePath; // Root directory for all logs for this system
} GeneralConfig_t;

typedef struct
{
	std::string ID;			  // Unique ID for this information piece
	std::string FetchMethod;  // Fetch method (Command|File|DBUS|GPIO)
	std::string FetchPath;	  // Command, dbus path, gpio number as relevant for the method
	int FetchFreqSecs;		  // Seconds between each fetch
	std::time_t LastUpdateAt; // Time of last update
} Info_t;

typedef struct
{
	std::string name;  // Name of the param
	std::string value; // Value for this param. Note: numbers are represented as strings as well
} Param_t;

typedef struct
{
	std::string ID;				  // Unique ID for this group eg: Inventory|Config|Versions|Errors|Stats
	std::vector<Info_t> InfoList; // List of information related to this component
} InfoGroup_t;

typedef struct
{
	std::string ID;						 // Unique ID for this part of the platform
	std::vector<Param_t> Params;		 // List of parameter & value pairs relevant for this component
	std::vector<InfoGroup_t> InfoGroups; // List of information Groups related to this component
} Component_t;

typedef struct
{
	std::string ID;						 // Unique ID for this part of the platform
	std::vector<Component_t> Components; // List of components under this section
} Section_t;

typedef struct
{
	GeneralConfig_t GeneralConfig;	 // General config for this system
	std::vector<Section_t> Sections; // List of Component Categories/Sections in the platform
} Profile_t;

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
			return true;
		}
	};

	template <>
	struct convert<Info_t>
	{
		static bool decode(const Node &node, Info_t &rhs)
		{
			rhs.ID = node["ID"].as<std::string>();
			if (node["FetchMethod"])
			{
				rhs.FetchMethod = node["FetchMethod"].as<std::string>();
			}
			if (node["FetchPath"])
			{
				rhs.FetchPath = node["FetchPath"].as<std::string>();
			}
			if (node["FetchFreqSecs"])
			{
				rhs.FetchFreqSecs = node["FetchFreqSecs"].as<int>();
			}
			rhs.LastUpdateAt = 0; // Init last read time to epoch
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
