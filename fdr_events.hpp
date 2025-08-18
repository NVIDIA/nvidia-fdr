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

#include "fdr_logs_schema.pb.h"

#include "fdr_policy.hpp"
#include "fdr_store.hpp"
#include "fdr_utils.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus/match.hpp>
#include <xyz/openbmc_project/Logging/Entry/server.hpp>

// Store section.ID, component.ID and infogroup.ID for book_of_errors
struct EventRecord
{
    Profile_t* profile;      // Profile ref
    std::string sectionID;   // Ex: GPU
    std::string componentID; // Ex: GPU0
    std::string infogroupID; // Ex: Error
};

class EventSignalHandler
{
  private:
    fdrpb::fdr_event fdr_event_data;
    fdrpb::fdr_event_details fdr_event_details_data;
    std::string eventObjPath;
    std::string eventIface;
    std::string eventMember;
    std::unique_ptr<sdbusplus::bus::match_t> eventHandlerMatcher;
    std::map<std::string, std::pair<std::shared_ptr<FDRStore>, EventRecord>>
        fdrDeviceEventsWriter;

    using eventPropertiesType = std::vector<std::pair<
        std::string,
        std::vector<std::pair<std::string,
                              std::variant<uint64_t, uint32_t, std::string,
                                           bool, std::vector<std::string>>>>>>;

    void eventParser(eventPropertiesType&);
    std::string getFDRDeviceName(std::string&);

  public:
    EventSignalHandler(
        std::string eventObjPath, std::string eventIface,
        std::string eventMember,
        std::map<std::string,
                 std::pair<std::shared_ptr<FDRStore>, EventRecord>>&
            fdrDeviceEventsWriter) :
        eventObjPath(eventObjPath), eventIface(eventIface),
        eventMember(eventMember), fdrDeviceEventsWriter(fdrDeviceEventsWriter)
    {}

    ~EventSignalHandler();

    void registerEventsSignal();
};

namespace rfEvent
{
void createLogEntry(const std::string& messageId,
                    const std::vector<std::string>& messageArgs,
                    const std::string& severity, const std::string& resolution,
                    const std::string& name = "System Event Log Entry",
                    sdbusplus::bus_t* busPtr = nullptr);
}
