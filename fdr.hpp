#pragma once

#include <spdlog/sinks/rotating_file_sink.h>

#include "fdr_policy.hpp"
#include "fdr_record.hpp"
#include "fdr_store.hpp"
#include "fdr_redfish.hpp"
#include "ppf_sanity.hpp"

class FlightDataRecorder_c
{
private:
	/* data */
	std::vector<Record *> RecList;

	std::string PPFName;
	std::string birthCertFilePath;
	std::unique_ptr<PPFSanity> SanityChecker;
	int FindAndLoadPlatformProfile(void);
	int ConvertPPFToStruct(const std::string filename);
	int ExecuteFingerPrintRules(void);
	std::unique_ptr<FDRStore> CreateParamDescriptionLog(void);

public:
	Profile_t profile;
	std::shared_ptr<spdlog::logger> log;
	RedfishClient *rfc;
	FlightDataRecorder_c(const std::string filename = std::string{});
	~FlightDataRecorder_c();
	void CreateRecords(void);
	void ReadOldRecords(void);
	void RefreshAndRecord(bool skipOnBootRec);
	void Compactor(void);
	void CollectAndArchieveBirthCertificate(void);
};

// We have a global fdr variable defined in main.cpp
extern FlightDataRecorder_c *fdr;