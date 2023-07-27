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

#include <spdlog/sinks/rotating_file_sink.h>
#include "fdr_common.hpp"
#include "fdr_policy.hpp"
#include "fdr_record.hpp"
#include "fdr_store.hpp"
#include "fdr_redfish.hpp"
#include "ppf_sanity.hpp"
#include "dbus_accessor.hpp"
#include "fdr_events.hpp"

class FlightDataRecorder_c
{
private:
	/* data */
	std::vector<Record *> RecList;
	std::time_t LastCompactWindowDirCreationSecsAt;   // Time of last compaction [paired with CompactionWindowSecs]
	const std::string CompactorBookKeeperName = "Compactor.log";
	// const std::string BookOfErrorKeeperName = "BookOfErrors.log";
	const std::string ParamDescKeeperName = "ParamDescription.log";
	std::unique_ptr<FDRStore> CompactorBookKeeperAppender;
	std::unique_ptr<FDRStore> fdrParamsWriter;

	std::string PPFName;
	std::string birthCertFilePath;
	std::unique_ptr<PPFSanity> SanityChecker;
	int FindAndLoadPlatformProfile(void);
	int ConvertPPFToStruct(const std::string filename);
	int ExecuteFingerPrintRules(void);
	void UpdateGlobVariables(bool needtoUpdateBootcounter);
	void CreateFdrHmcAlive(void);
	std::unique_ptr<FDRStore> CreateKeeperWriter(const std::string dirName, const std::string filename);
	void DeleteSpecificRecords(std::string recordSubName);
	void CreateSpecificRecords(std::string recordSubName);

	void InitLogger();

	LeakyBucket *ExceptionRateLimiter;
	void InitExceptionRateLimiter();

	// all private functions related to Compactor
	void CheckCompactionWindowExpiry(void);
	bool CompactorCheckBookOfErrors(const std::string directoryTocompact,
							uint64_t leastWindowTimestamp,
							uint64_t farWindowTimestamp,
							std::vector<fdrpb::fdr_book_of_errors> &errorMap);
	void CompactorBookKeeperRemoveEntry(std::string directoryTocompact);
	void CompactorBookKeeperAppendEntry(void);
	void CompactorBookKeeperCleanEntries(void);
	void CompactorRemoveSamplesLogfiles(std::string directoryTocompact);
	std::string CompactorGetDirectoryToCompact(int numberOfDirToLook);
	void CompactorCreateStatFiles(const std::string directoryTocompact,
								  uint64_t &leastWindowTimestamp,
								  uint64_t &farWindowTimestamp);
	void CompactorCreateHighFidelityFiles(const std::string directoryTocompact,
										  std::vector<fdrpb::fdr_book_of_errors> errorList);
	void CompactorAppendStatFile(FDRStore &fdrstats, std::map<unsigned int, fdrpb::fdr_stat> stats_so_far);
	void CompactorCollectHiFidelityRecords(std::string componentId,
								  std::string infogroupID,
								  fdrpb::fdr_sample readRecord,
								  std::map<std::string, std::map<std::string, std::vector<fdrpb::fdr_sample>>> &hifiRecords);
	void CompactorEngine(std::string directoryTocompact);

	// all private functions related to Book Of Errors
	void SetBookOfErrorsRecord(unsigned int paramID, std::string sectionID, std::string componentID, std::string paramClass,
                                                 const char *value, time_t current_time, std::string bookOfErrorsFileName);

	bool CompareMessageWithLog(const fdrpb::fdr_book_of_errors& errMssg, const std::string& logFile);

public:

	const std::string BookOfErrorKeeperName = "BookOfErrors.log";
	std::unique_ptr<FDRStore> fdrbookoferrorswriter;
	// Map of device name to FDRStore object for streaming AML events on all devices
	std::map<std::string, std::pair<std::shared_ptr<FDRStore>, EventRecord>>
		fdrDeviceErrorsWriter;
    fdrpb::fdr_book_of_errors book_of_errors; // Data that will land in book of errors
	Profile_t profile;
	std::shared_ptr<spdlog::logger> log;
	RedfishClient *rfc;
	FlightDataRecorder_c(const std::string filename = std::string{});
	~FlightDataRecorder_c();
	void CreateSamplesWriter(Profile_t &profile, std::string compClass,
							 std::string compID, std::string paramClass,
							 std::shared_ptr<FDRStore> &fdrLogWriter,
							 const std::string& fileTimestamp);
	void CreateRecords(void);
	void ReadOldRecords(void);
	void CollectAndArchieveBirthCertificate(void);
	void RefreshAndRecord(void);
	inline void Compactor(void)
	{
		CheckCompactionWindowExpiry();
	}
	void BookOfErrorEngine(std::string infoID, unsigned int paramID, std::string sectionID, std::string componentID, std::string paramClass,
                                             time_t current_time, PropertyVariant val);
	void CheckForErrorsToUpdateBookOfErrors(void);
    void CheckExceptionRateLimit();
	void initEventsSignalRegistration();
};

// We have a global fdr variable defined in main.cpp
extern FlightDataRecorder_c *fdr;
