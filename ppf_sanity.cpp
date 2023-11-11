/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <exception>
#include "yaml-cpp/yaml.h" //yaml-cpp lib used for parsing input (yaml) files

#include "fdr_common.hpp"
#include "fdr_policy.hpp"
#include "ppf_sanity.hpp"
#include "fdr_log.hpp"

// Sanity check the Platform Profile File [PPF]
int PPFSanity::SanityTestPPF(const std::string PPFName){
	std::map<std::string, std::vector<InfoGroup_t>> ComponentsMap;
	try {
		YAML::Node PlatformProfile = YAML::LoadFile(PPFName);
		ComponentsMap = CreateComponentsMap(PlatformProfile);
	} catch(std::exception& e) {
		fdrlog::warn("Exception while parsing {}: {}", PPFName, e.what());
		return EXIT_FAILURE;
	}
	int retval = ValidatePPFComponents(ComponentsMap);
	return retval;
}

// Create a std::map with Component name (GPU, FPGA etc.) as key, and the InfoGroups from PPF for that component as the value.
// Throws exception if there is any error parsing the components
std::map<std::string, std::vector<InfoGroup_t>> PPFSanity::CreateComponentsMap(const YAML::Node& PlatformProfile) {
	std::map<std::string, std::vector<InfoGroup_t>> ComponentsMap;
	for (YAML::const_iterator it = PlatformProfile.begin(); it != PlatformProfile.end(); ++it) {
		std::string component_name = it->first.as<std::string>();
		if ((component_name.compare("FingerPrint") != 0) & (component_name.compare("GeneralConfig") != 0) & (component_name.compare("Sections") != 0) & 
		(component_name.compare("Preconditions") != 0)) {
			std::vector<InfoGroup_t> InfoGroups = PlatformProfile[component_name]["InfoGroups"].as<std::vector<InfoGroup_t>>();
			ComponentsMap.insert(std::pair<std::string, std::vector<InfoGroup_t>>(component_name, InfoGroups));
		}
	}
	return ComponentsMap;
}

// Validate the Components in the PPF
int PPFSanity::ValidatePPFComponents(const std::map<std::string, std::vector<InfoGroup_t>>& ComponentsMap) {
	int retval = FDR_SUCCESS;
	for (std::map<std::string, std::vector<InfoGroup_t>>::const_iterator it1 = ComponentsMap.begin(); it1 != ComponentsMap.end(); ++it1) {
		for (std::vector<InfoGroup_t>::const_iterator it2 = it1->second.begin(); it2 != it1->second.end(); ++it2) {
			for (std::vector<Info_t>::const_iterator it3 = it2->InfoList.begin(); it3 != it2->InfoList.end(); ++it3) {
				retval |= UniqueParamChecker<unsigned int>(it3->ParamID, "ParamID", it2->ID, it1->first)
						| UniqueParamChecker<std::string>(it3->ID, "ID", it2->ID, it1->first);
			}
		}
	}
	return retval;
}

// Checks if values assigned to a certain parameter under a specific class of a component are unique.
template<typename T>
int PPFSanity::UniqueParamChecker(const T& paramValue, std::string paramName, std::string paramClass, std::string component) {
	int retval = FDR_SUCCESS;

	static std::string prev_paramClass, prev_component;
	static std::vector<T> all_paramValues;
	if ((component != prev_component) || (paramClass != prev_paramClass)) {
		// Need to start with a new paramIDs list
		all_paramValues.clear();
		prev_component = component;
		prev_paramClass = paramClass;
	}
	// Check for duplicate ParamID
	typename std::vector<T>::iterator it = std::find(all_paramValues.begin(), all_paramValues.end(), paramValue);
	if (it == all_paramValues.end()) {
		// The ParamID hasn't been used yet!
		all_paramValues.push_back(paramValue);
	}
	else {
		// The ParamID is repeated
		fdrlog::error ("Error: Duplicate {} {} assigned in {} of {}", paramName, paramValue, paramClass, component);
		retval = FDR_ERR_GENFAILURE;
	}
	return retval;
}
