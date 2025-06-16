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

#include "dbus_accessor.hpp"
#include "fdr_common.hpp"
#include "fdr_events.hpp"
#include "fdr_policy.hpp"
#include "fdr_record.hpp"
#include "fdr_redfish.hpp"
#include "fdr_store.hpp"
#include "ppf_sanity.hpp"

#include <sdbusplus/bus/match.hpp>
#include <sdeventplus/clock.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/utility/timer.hpp>

using sdeventplus::Clock;
using sdeventplus::ClockId;
using sdeventplus::Event;

#define FDR_TIMER_EVENT_ENABLED

constexpr auto clockId = sdeventplus::ClockId::Monotonic;
using Timer = sdeventplus::utility::Timer<clockId>;
using groupPollRecords = std::pair<std::string, std::vector<Record*>>;

class FlightDataRecorder_c
{
  private:
    /* data */
    std::vector<Record*> RecList;
    // This map includes only Poll records and used for both fetching[refresh]
    // and storing[store]
    std::map<int, std::vector<Record*>> RecListPollMap;
    // This map includes only Subscribe records and used only for storing[store]
    // as fetching[refresh] is taken by the subscription events
    std::map<int, std::vector<Record*>> RecListSubscribeStoreMap;
    // This map includes only Group Poll records and used for both
    // fetching[refresh] and storing[store]
    std::map<int, std::vector<groupPollRecords>> RecListGrpPollMap;
    sdeventplus::Event FdrEvents = sdeventplus::Event::get_default();
    std::time_t
        LastCompactWindowDirCreationSecsAt; // Time of last compaction [paired
                                            // with CompactionWindowSecs]
    std::time_t
        LastCompactSubWindowDirCreationSecsAt; // Time of last compaction
                                               // [paired with
                                               // CompactionWindowSecs]
    const std::string BootEventKeeperName = "BootEvent.dat";
    const std::string CompactorBookKeeperName = "Compactor.dat";
    const std::string BookOfErrorKeeperName = "BookOfErrors.dat";
    const std::string ParamDescKeeperName = "ParamDescription.dat";
    std::unique_ptr<FDRStore> CompactorBookKeeperAppender;
    std::unique_ptr<FDRStore> fdrParamsWriter;
    // just for storing all the Timer objects, so that these objects doesn't go
    // out of scope
    std::vector<Timer> allFdrTimers;

    std::string PPFName;
    std::string birthCertFilePath;
    std::unique_ptr<PPFSanity> SanityChecker;
    std::vector<std::pair<std::string, std::string>> subscribedPaths;
    std::vector<std::unique_ptr<sdbusplus::bus::match_t>> eventHandlerMatcher;

    int FindAndLoadPlatformProfile(void);
    int ConvertPPFToStruct(const std::string filename);
    int ExecuteFingerPrintRules(void);
    int ExecutePreconditionRules(void);
    void UpdateGlobVariables(bool needtoUpdateBootcounter);
    void UpdateBootEventLog(void);
    void CreateFdrHmcAlive(void);
    std::unique_ptr<FDRStore> CreateKeeperWriter(const std::string dirName,
                                                 const std::string filename);
    void DeleteSpecificRecords(std::string recordSubName);
    void ModifySpecificRecords(std::string recordSubName);

    LeakyBucket* ExceptionRateLimiter;
    void InitExceptionRateLimiter();

    // all private functions related to Compactor
    int CheckCompactionWindowExpiry(bool viaTimerSkipChecks,
                                    std::time_t current_time);
    int CheckCompactionSubWindowExpiry(bool viaTimerSkipChecks,
                                       std::time_t current_time,
                                       int mainWindowCompactStatus);
    void CompactionWindowExpiryCleanUp(int mainWindowCompactStatus);
    bool CompactorCheckBookOfErrors(
        const std::string directoryTocompact, uint64_t leastWindowTimestamp,
        uint64_t farWindowTimestamp,
        std::vector<fdrpb::fdr_book_of_errors>& errorMap);
    void CompactorBookKeeperRemoveEntry(std::string directoryTocompact);
    uint32_t CompactorGetItsDataFormat(std::string directoryTocompact);
    void CompactorBookKeeperAppendEntry(void);
    void CompactorBookKeeperCleanEntries(void);
    void CompactorRemoveSamplesLogfiles(std::string directoryTocompact);
    std::string CompactorGetDirectoryToCompact(int numberOfDirToLook,
                                               int& dirCounter);
    void CompactorGetLeastAndFarTimestamp(const std::string directoryTocompact,
                                          uint64_t& leastWindowTimestamp,
                                          uint64_t& farWindowTimestamp);
    void CompactorCreateHighFidelityFiles(
        const std::string directoryTocompact,
        std::vector<fdrpb::fdr_book_of_errors> errorList);
    void CompactorEngine(std::string directoryTocompact);

    // all private functions related to Book Of Errors
    void SetBookOfErrorsRecord(unsigned int paramID, std::string componentID,
                               const char* value, time_t current_time);

    bool CompareMessageWithLog(const fdrpb::fdr_book_of_errors& errMssg);
    void PrintRecListPollSubscribeMap(void);
    void PollRecordTimerCBEngine(int fetchFreqSecKey);
    void GroupPollRecordTimerCBEngine(int fetchFreqSecKey);
    void SubscribeStoreRecordTimerCBEngine(int storeFreqSec);
    void CompactionWindowTimerCBEngine(void);
    void CompactionSubWindowTimerCBEngine(void);

  public:
    std::unique_ptr<FDRStore> fdrbookoferrorswriter;
    // Map of device name to FDRStore object for streaming AML events on all
    // devices
    std::map<std::string, std::pair<std::shared_ptr<FDRStore>, EventRecord>>
        fdrDeviceErrorsWriter;
    static std::unordered_map<std::string, Record*>
        subscribedRecListMap; // Optimized map to fetch record in O(1) via
                              // unique composite key
    fdrpb::fdr_book_of_errors
        book_of_errors;       // Data that will land in book of errors
    Profile_t profile;
    RedfishClient* rfc;
    FlightDataRecorder_c(const std::string filename = std::string{});
    ~FlightDataRecorder_c();
    void CreateSamplesWriter(Profile_t& profile, std::string compClass,
                             std::string compID, std::string paramClass,
                             std::string fileExtention,
                             std::shared_ptr<FDRStore>& fdrLogWriter,
                             const std::string& fileTimestamp);
    void CreateRecords(void);
    void CollectAndArchieveBirthCertificate(void);
    void MakeBirthCertificateDeleteSafe(void);
    void RefreshAndStore(bool viaTimerSkipChecks,
                         const std::vector<Record*>& recordListToRefresh);
    void RefreshAndStore(bool viaTimerSkipChecks);
    void GroupRefreshAndStore(bool viaTimerSkipChecks,
                              const std::vector<groupPollRecords>& groupList);
    void GroupRefreshAndStore(bool viaTimerSkipChecks);
    void StoreSubscribeRecords(const std::vector<Record*>& recordListToStore);
    void Compactor(bool viaTimerSkipChecks);
    void BookOfErrorEngine(std::string infoID, unsigned int paramID,
                           std::string componentID, time_t current_time,
                           PropertyVariant val);
    void CheckForErrorsToUpdateBookOfErrors(void);
    void CheckExceptionRateLimit();
    void initEventsSignalRegistration();
    void InitTimerEvents(void);
    void RunEventLoop(void);
    void initRecordsSignalRegistration();
    void CleanupPlatformFiles(void);
    int CheckAvailableFdrPartitionDiskSize(void);
    void CheckFdrPartitionDiskUsageAndExit(void);

    /**
     * @brief This is the callback which handles DBUS properties changes
     */
    static void dbusEventHandlerCallback(sdbusplus::message::message& msg);
};

// We have a global fdr variable defined in main.cpp
extern FlightDataRecorder_c* fdr;
