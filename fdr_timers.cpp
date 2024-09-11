/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include "fdr.hpp"
#include "fdr_log.hpp"
#include "fdr_utils.hpp"

// for debugging
void FlightDataRecorder_c::PrintRecListPollSubscribeMap(void)
{
    fdrlog::info(
        "----------------------Elements in RecListPollMap:----------------------");
    for (const auto& entry : RecListPollMap)
    {
        int fetchFreqSec = entry.first;
        const std::vector<Record*> recListPoll = entry.second;

        fdrlog::info("\tfetchFreqSec: {};count: {}", fetchFreqSec,
                     recListPoll.size());

        // generic debug print which prints all the entires
        fdrlog::debug("\t\tvalues: ");
        for (auto& rec : recListPoll)
        {
            fdrlog::debug("\t\t{}:{}:{}:{}:{}{}", rec->info.FetchFreqSecs,
                          rec->info.StoreFreqSecs, rec->GetComponentID(),
                          rec->GetInfoGroupID(), rec->GetInfoListID(),
                          (rec->info.FetchFreqSecs != rec->info.StoreFreqSecs)
                              ? "    <<<<<<<<<<<<<<"
                              : "");
        }

        // specific debug print which prints all the entires with not matching
        // fetch anf store frequencies. These prints are made as info, so that
        // it comes in log be default for finding the record which have not
        // matching fetch anf store frequencies.
        fdrlog::info("\t\tNot matching values: ");
        for (auto& rec : recListPoll)
        {
            if (rec->info.FetchFreqSecs != rec->info.StoreFreqSecs)
            {
                fdrlog::info("\t\t{}:{}:{}:{}:{}    <<<<<<<<<<<<<<",
                             rec->info.FetchFreqSecs, rec->info.StoreFreqSecs,
                             rec->GetComponentID(), rec->GetInfoGroupID(),
                             rec->GetInfoListID());
            }
        }
    }

    fdrlog::info(
        "----------------------Elements in RecListGrpPollMap:----------------------");
    for (const auto& entry : RecListGrpPollMap)
    {
        int fetchFreqSec = entry.first;
        for (auto& recGroup : entry.second)
        {
            const std::vector<Record*>& records = recGroup.second;

            fdrlog::info("\tGroup Key {} fetchFreqSec: {};count: {}",
                         recGroup.first, fetchFreqSec, records.size());

            // generic debug print which prints all the entires
            fdrlog::debug("\t\tvalues: ");
            for (auto& rec : records)
            {
                fdrlog::debug(
                    "\t\t{}:{}:{}:{}:{}{}", rec->info.FetchFreqSecs,
                    rec->info.StoreFreqSecs, rec->GetComponentID(),
                    rec->GetInfoGroupID(), rec->GetInfoListID(),
                    (rec->info.FetchFreqSecs != rec->info.StoreFreqSecs)
                        ? "    <<<<<<<<<<<<<<"
                        : "");
            }

            // specific debug print which prints all the entires with not
            // matching fetch anf store frequencies. These prints are made as
            // info, so that it comes in log be default for finding the record
            // which have not matching fetch anf store frequencies.
            fdrlog::info("\t\tNot matching values: ");
            for (auto& rec : records)
            {
                if (rec->info.FetchFreqSecs != rec->info.StoreFreqSecs)
                {
                    fdrlog::info("\t\t{}:{}:{}:{}:{}    <<<<<<<<<<<<<<",
                                 rec->info.FetchFreqSecs,
                                 rec->info.StoreFreqSecs, rec->GetComponentID(),
                                 rec->GetInfoGroupID(), rec->GetInfoListID());
                }
            }
        }
    }

    fdrlog::info(
        "----------------------Elements in RecListSubscribeStoreMap:----------------------");
    for (const auto& entry : RecListSubscribeStoreMap)
    {
        int storeFreqSec = entry.first;
        const std::vector<Record*> recListSubscribeStore = entry.second;

        fdrlog::info("\tstoreFreqSec: {}; count: {}", storeFreqSec,
                     recListSubscribeStore.size());

        // generic debug print which prints all the entires
        fdrlog::debug("\t\tvalues: ");
        for (auto& rec : recListSubscribeStore)
        {
            fdrlog::debug("\t\t{}:{}:{}:{}:{}{}", rec->info.FetchFreqSecs,
                          rec->info.StoreFreqSecs, rec->GetComponentID(),
                          rec->GetInfoGroupID(), rec->GetInfoListID(),
                          (rec->info.FetchFreqSecs != rec->info.StoreFreqSecs)
                              ? "    <<<<<<<<<<<<<<"
                              : "");
        }

        // specific debug print which prints all the entires with not matching
        // fetch anf store freuencies These prints are made as info, so that it
        // comes in log be default for finding the record which have not
        // matching fetch anf store frequencies.
        fdrlog::info("\t\tNot matching values: ");
        for (auto& rec : recListSubscribeStore)
        {
            if (rec->info.FetchFreqSecs != rec->info.StoreFreqSecs)
            {
                fdrlog::info("\t\t{}:{}:{}:{}:{}    <<<<<<<<<<<<<<",
                             rec->info.FetchFreqSecs, rec->info.StoreFreqSecs,
                             rec->GetComponentID(), rec->GetInfoGroupID(),
                             rec->GetInfoListID());
            }
        }
    }
}

// Actual worker function for the Poll Records timer event
// This timer event callback do both fetching and storing of the given Poll
// record list.
void FlightDataRecorder_c::PollRecordTimerCBEngine(int fetchFreqSecKey)
{
    // fdrlog::info("PollRecordTimerCBEngine Start fetchFreqSecKey: {}",
    // fetchFreqSecKey);
    //  check if the key exist.
    if (RecListPollMap.count(fetchFreqSecKey) != 0)
    {
        // should refresh all the records having the same fetchFreqSec
        const auto& recListPoll = RecListPollMap[fetchFreqSecKey];

        // for debugging
        // fdrlog::debug("PollRecordTimerCBEngine: fetchFreqSecKey: {}",
        // fetchFreqSecKey); for (auto &rec : recListPoll) {
        //     fdrlog::debug("{}", rec->info.ID);
        // }

        // real work of this timer callback
        RefreshAndStore(true, recListPoll);
    }
    // fdrlog::info("PollRecordTimerCBEngine Done fetchFreqSecKey: {}",
    // fetchFreqSecKey);
}

// Actual worker function for the Group Poll Records timer event
// This timer event callback do both fetching and storing of the given Poll
// record list.
void FlightDataRecorder_c::GroupPollRecordTimerCBEngine(int fetchFreqSecKey)
{
    // fdrlog::info("GroupPollRecordTimerCBEngine Start fetchFreqSecKey: {}",
    // fetchFreqSecKey);
    //  check if the key exist.
    if (RecListGrpPollMap.count(fetchFreqSecKey) != 0)
    {
        // should refresh all the records having the same fetchFreqSec
        const auto& recListGroupPoll = RecListGrpPollMap[fetchFreqSecKey];

        // for debugging
        // for (auto &recGroup : recListGroupPoll) {
        //     fdrlog::info("GroupPollRecordTimerCBEngine: fetchFreqSecKey:{}
        //     key:{}", fetchFreqSecKey, recGroup.first);
        // }

        GroupRefreshAndStore(true, recListGroupPoll);
    }
    // fdrlog::info("GroupPollRecordTimerCBEngine Done fetchFreqSecKey: {}",
    // fetchFreqSecKey);
}

// Actual worker function for the Subscribe Records timer event for storing
// This timer event callback do only storing of the given Subscribe record list.
void FlightDataRecorder_c::SubscribeStoreRecordTimerCBEngine(int storeFreqSec)
{
    // fdrlog::info("SubscribeStoreRecordTimerCBEngine Start storeFreqSec: {}",
    // storeFreqSec);
    //  check if the key exist.
    if (RecListSubscribeStoreMap.count(storeFreqSec) != 0)
    {
        // should call store all the records having the same storeFreqSec
        const auto& recListSubscribeStore =
            RecListSubscribeStoreMap[storeFreqSec];

        // for debugging
        // fdrlog::debug("SubscribeStoreRecordTimerCBEngine: storeFreqSec: {}",
        // storeFreqSec); for (auto &rec : recListSubscribeStore) {
        //     fdrlog::debug("{}", rec->info.ID);
        // }

        // real work of this timer callback which is to call only the Store() of
        // its resource object as Refresh() would have been done already by its
        // Susbcription signal handler.
        StoreSubscribeRecords(recListSubscribeStore);
    }
    // fdrlog::info("SubscribeStoreRecordTimerCBEngine Done storeFreqSec: {}",
    // storeFreqSec);
}

// Actual worker function for the compaction window timer event
void FlightDataRecorder_c::CompactionWindowTimerCBEngine(void)
{
    Compactor(true);
}

// Actual worker function for the compaction window timer event
void FlightDataRecorder_c::CompactionSubWindowTimerCBEngine(void)
{
    CheckCompactionSubWindowExpiry(true, std::time(nullptr),
                                   FDR_ERR_MAIN_WINDOW_NOT_EXPIRED);
}

// Initializing all the timers like records[of Poll type] refresh time,
// compaction window and compaction sub window.
void FlightDataRecorder_c::InitTimerEvents(void)
{
    fdrlog::info(
        "Enable the timers for all the POLL records, Subscribe records for Store, Compaction window & sub window!");

    auto& sdbusConn = getBus();
    sdbusConn.attach_event(FdrEvents.get(), SD_EVENT_PRIORITY_NORMAL);
    // for debugging:
    PrintRecListPollSubscribeMap();

    // ------------------------------------------------------------------------------------------
    // step 1: Init Timer for all the Poll records.
    // This timer event callback do both fetching and storing of the given Poll
    // record list.
    for (const auto& entry : RecListPollMap)
    {
        // timers value
        int fetchFreqSec = entry.first;

        // define Timer call back
        auto PollRecordTimerCB = [&](Timer&, int fetchFreqSec) {
            fdrlog::debug("PollRecordTimerCB: fetchFreqSec: {}", fetchFreqSec);
            PollRecordTimerCBEngine(fetchFreqSec);
        };
        auto PollRecordHandler = std::bind(PollRecordTimerCB,
                                           std::placeholders::_1, fetchFreqSec);

        // register a timer and its call back to be called
        fdrlog::info("Registering for Poll Record time: {}", fetchFreqSec);
        Timer PollRecordTimer(FdrEvents, std::move(PollRecordHandler),
                              std::chrono::seconds{fetchFreqSec});

        // push to global variable to not to loose the scope of local pointer
        allFdrTimers.push_back(PollRecordTimer);
    }

    // ------------------------------------------------------------------------------------------
    // step 2: Init Timer for all the Group Poll records.
    // This timer event callback do both fetching and storing of the given Poll
    // record list.
    for (const auto& entry : RecListGrpPollMap)
    {
        // timers value
        int fetchFreqSec = entry.first;

        // define Timer call back
        auto PollRecordTimerCB = [&](Timer&, int fetchFreqSec) {
            fdrlog::debug("Group PollRecordTimerCB: fetchFreqSec: {}",
                          fetchFreqSec);
            GroupPollRecordTimerCBEngine(fetchFreqSec);
        };
        auto GroupPollRecordHandler =
            std::bind(PollRecordTimerCB, std::placeholders::_1, fetchFreqSec);

        // register a timer and its call back to be called
        fdrlog::info("Registering for Group Poll Record time: {}",
                     fetchFreqSec);
        Timer GroupPollRecordTimer(FdrEvents, std::move(GroupPollRecordHandler),
                                   std::chrono::seconds{fetchFreqSec});

        // push to global variable to not to loose the scope of local pointer
        allFdrTimers.push_back(GroupPollRecordTimer);
    }

    // ------------------------------------------------------------------------------------------
    // step 3: Init Timer for all the Subscription records.
    // This timer event callback do only storing of the given Subscribe record
    // list.
    for (const auto& entry : RecListSubscribeStoreMap)
    {
        // timers value
        int storeFreqSec = entry.first;

        // define Timer call back
        auto SubscribeStoreRecordTimerCB = [&](Timer&, int storeFreqSec) {
            fdrlog::debug("SubscribeStoreRecordTimerCB: storeFreqSec: {}",
                          storeFreqSec);
            SubscribeStoreRecordTimerCBEngine(storeFreqSec);
        };
        auto SubscribeStoreRecordHandler = std::bind(
            SubscribeStoreRecordTimerCB, std::placeholders::_1, storeFreqSec);

        // register a timer and its call back to be called
        fdrlog::info("Registering for Subscribe Store Record time: {}",
                     storeFreqSec);
        Timer SubscribeStoreRecordTimer(FdrEvents,
                                        std::move(SubscribeStoreRecordHandler),
                                        std::chrono::seconds{storeFreqSec});

        // push to global variable to not to loose the scope of local pointer
        allFdrTimers.push_back(SubscribeStoreRecordTimer);
    }

    // ------------------------------------------------------------------------------------------
    // step 4: Init Timer for CompactionWindowSecs
    // define Timer call back
    auto CompactionWindowTimerCB = [&](Timer&, int compactionWindowSecs) {
        fdrlog::debug("CompactionWindowTimerCB: compactionWindowSecs: {}",
                      compactionWindowSecs);
        CompactionWindowTimerCBEngine();
    };
    auto CompactionWindowHandler =
        std::bind(CompactionWindowTimerCB, std::placeholders::_1,
                  profile.GeneralConfig.CompactionWindowSecs);

    // register a timer and its call back to be called
    fdrlog::info("Registering for CompactionWindow time: {}",
                 profile.GeneralConfig.CompactionWindowSecs);
    Timer CompactionWindowTimer(
        FdrEvents, std::move(CompactionWindowHandler),
        std::chrono::seconds{profile.GeneralConfig.CompactionWindowSecs});

    // push to global variable to not to loose the scope of local pointer
    allFdrTimers.push_back(CompactionWindowTimer);

    // ------------------------------------------------------------------------------------------
    // step 4: Init Timer for CompactionSubWindowSecs
    // define Timer call back
    auto CompactionSubWindowTimerCB = [&](Timer&, int CompactionSubWindowSecs) {
        fdrlog::debug("CompactionSubWindowTimerCB: CompactionSubWindowSecs: {}",
                      CompactionSubWindowSecs);
        CompactionSubWindowTimerCBEngine();
    };
    auto CompactionSubWindowHandler =
        std::bind(CompactionSubWindowTimerCB, std::placeholders::_1,
                  profile.GeneralConfig.CompactionSubWindowSecs);

    // register a timer and its call back to be called
    fdrlog::info("Registering for CompactionSubWindow time: {}",
                 profile.GeneralConfig.CompactionSubWindowSecs);
    Timer CompactionSubWindowTimer(
        FdrEvents, std::move(CompactionSubWindowHandler),
        std::chrono::seconds{profile.GeneralConfig.CompactionSubWindowSecs});

    // push to global variable to not to loose the scope of local pointer
    allFdrTimers.push_back(CompactionSubWindowTimer);

    // ------------------------------------------------------------------------------------------
}

void FlightDataRecorder_c::RunEventLoop(void)
{
    fdrlog::info("starting FdrEvents.loop()");
    FdrEvents.loop();
    fdrlog::info("FdrEvents.loop() terminated");
}
