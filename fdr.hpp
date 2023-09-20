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

#include <spdlog/spdlog.h>
#include "fdr_common.hpp"
#include "fdr_policy.hpp"
#include "fdr_record.hpp"
#include "fdr_store.hpp"
#include "fdr_redfish.hpp"
#include "ppf_sanity.hpp"
#include "dbus_accessor.hpp"
#include "fdr_events.hpp"

#include <sdeventplus/event.hpp>
using sdeventplus::Event;

#define FDR_TIMER_EVENT_ENABLED

class FlightDataRecorder_c
{
private:
	/* data */
	std::vector<Record *> RecList;
	// This map includes only Poll records and used for both fetching[refresh] and storing[store]
	std::map<int, std::vector<Record *>> RecListPollMap;
	// This map includes only Subscribe records and used only for storing[store] as
	// fetching[refresh] is taken by the subscription events
	std::map<int, std::vector<Record *>> RecListSubscribeStoreMap;
	sdeventplus::Event FdrEvents = sdeventplus::Event::get_default();
	std::time_t LastCompactWindowDirCreationSecsAt;   // Time of last compaction [paired with CompactionWindowSecs]
	std::time_t LastCompactSubWindowDirCreationSecsAt;   // Time of last compaction [paired with CompactionWindowSecs]
	const std::string CompactorBookKeeperName = "Compactor.log";
	const std::string BookOfErrorKeeperName = "BookOfErrors.log";
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
	void ModifySpecificRecords(std::string recordSubName);

	void InitLogger();

	LeakyBucket *ExceptionRateLimiter;
	void InitExceptionRateLimiter();

	// all private functions related to Compactor
	int CheckCompactionWindowExpiry(bool viaTimerSkipChecks, std::time_t current_time);
	int CheckCompactionSubWindowExpiry(bool viaTimerSkipChecks, std::time_t current_time, int mainWindowCompactStatus);
	void CompactionWindowExpiryCleanUp(int mainWindowCompactStatus);
	bool CompactorCheckBookOfErrors(const std::string directoryTocompact,
							uint64_t leastWindowTimestamp,
							uint64_t farWindowTimestamp,
							std::vector<fdrpb::fdr_book_of_errors> &errorMap);
	void CompactorBookKeeperRemoveEntry(std::string directoryTocompact);
	void CompactorBookKeeperAppendEntry(void);
	void CompactorBookKeeperCleanEntries(void);
	void CompactorRemoveSamplesLogfiles(std::string directoryTocompact);
	std::string CompactorGetDirectoryToCompact(int numberOfDirToLook);
	void CompactorGetLeastAndFarTimestamp(const std::string directoryTocompact,
								  uint64_t &leastWindowTimestamp,
								  uint64_t &farWindowTimestamp);
	void CompactorCreateHighFidelityFiles(const std::string directoryTocompact,
										  std::vector<fdrpb::fdr_book_of_errors> errorList);
	void CompactorEngine(std::string directoryTocompact);

	// all private functions related to Book Of Errors
	void SetBookOfErrorsRecord(unsigned int paramID, std::string sectionID, std::string componentID, std::string paramClass,
                                                 const char *value, time_t current_time, std::string bookOfErrorsFileName);

	bool CompareMessageWithLog(const fdrpb::fdr_book_of_errors& errMssg, const std::string& logFile);
	void PrintRecListPollSubscribeMap(void);
	void PollRecordTimerCBEngine(int fetchFreqSecKey);
	void SubscribeStoreRecordTimerCBEngine(int storeFreqSec);
	void CompactionWindowTimerCBEngine(void);
	void CompactionSubWindowTimerCBEngine(void);

public:

	std::unique_ptr<FDRStore> fdrbookoferrorswriter;
	// Map of device name to FDRStore object for streaming AML events on all devices
	std::map<std::string, std::pair<std::shared_ptr<FDRStore>, EventRecord>> fdrDeviceErrorsWriter;
    fdrpb::fdr_book_of_errors book_of_errors; // Data that will land in book of errors
	Profile_t profile;
	std::shared_ptr<spdlog::logger> log;
	RedfishClient *rfc;
	FlightDataRecorder_c(const std::string filename = std::string{});
	~FlightDataRecorder_c();
	void CreateSamplesWriter(Profile_t &profile, std::string compClass,
							 std::string compID, std::string paramClass, std::string fileExtention,
							 std::shared_ptr<FDRStore> &fdrLogWriter,
							 const std::string& fileTimestamp);
	void CreateRecords(void);
	void CollectAndArchieveBirthCertificate(void);
	void RefreshAndStore(bool viaTimerSkipChecks, const std::vector<Record *>& recordListToRefresh);
	void RefreshAndStore(bool viaTimerSkipChecks);
	void StoreSubscribeRecords(const std::vector<Record *>& recordListToStore);
	void Compactor(bool viaTimerSkipChecks);
	void BookOfErrorEngine(std::string infoID, unsigned int paramID, std::string sectionID, std::string componentID, std::string paramClass,
                                             time_t current_time, PropertyVariant val);
	void CheckForErrorsToUpdateBookOfErrors(void);
    void CheckExceptionRateLimit();
	void initEventsSignalRegistration();
	void InitTimerEvents(void);
	void RunEventLoop(void);
};

// We have a global fdr variable defined in main.cpp
extern FlightDataRecorder_c *fdr;
