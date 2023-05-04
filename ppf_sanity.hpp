#pragma once

class PPFSanity 
{
private:
	std::map<std::string, std::vector<InfoGroup_t>> CreateComponentsMap(const YAML::Node& PlatformProfile);
	int ValidatePPFComponents(const std::map<std::string, std::vector<InfoGroup_t>>& ComponentsMap);
	template<typename T>
	int UniqueParamChecker(const T& paramValue,  std::string paramName, std::string paramClass, std::string component);
public:
    int SanityTestPPF(const std::string PPFName);
};