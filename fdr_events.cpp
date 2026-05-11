/*
 Copyright (c) 2024, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include "fdr_events.hpp"

#include "fdr.hpp"
#include "fdr_log.hpp"
#include "fdr_policy.hpp"
#include "property_variant.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/message.hpp>

#include <iostream>
#include <map>
#include <sstream>
#include <type_traits>
#include <variant>

/** @brief Helper to fetch device id from device name */
std::string getDeviceId(const std::string& deviceName)
{
    std::string deviceId;
    // Find the position of the last underscore
    std::size_t lastUnderscorePos = deviceName.find_last_of('_');
    if (lastUnderscorePos != std::string::npos)
    {
        // Extract the substring after the underscore
        deviceId = deviceName.substr(lastUnderscorePos + 1);
    }

    return deviceId;
}

// TODO: Update fdr device name to match HMC DAT device name
/** @brief Method to convert event device name to FDR device name */
std::string EventSignalHandler::getFDRDeviceName(std::string& deviceName)
{
    std::string fdrDeviceName;
    // Translate GPU_SXM_1 to GPU1
    if (deviceName.find("GPU") != std::string::npos)
    {
        auto deviceId = getDeviceId(deviceName);
        fdrDeviceName = "HGX_GPU_SXM_" + deviceId;
    }
    // Translate NVSwitch_0 to NVSwitch0
    else if (deviceName.find("NVSwitch") != std::string::npos)
    {
        auto deviceId = getDeviceId(deviceName);
        fdrDeviceName = "HGX_NVSwitch_" + deviceId;
    }
    // Translate HGX_Baseboard_0 to Baseboard0
    else if (deviceName.find("Baseboard") != std::string::npos)
    {
        auto deviceId = getDeviceId(deviceName);
        fdrDeviceName = "HGX_Chassis_" + deviceId;
    }

    return fdrDeviceName;
}

/* Sample of event message to be parsed by handler
string "xyz.openbmc_project.Logging.Entry"
array [
    dict entry(
        string "Id"
        variant                   uint32 8
    )
    dict entry(
        string "Timestamp"
        variant                   uint64 1578862245336
    )
    dict entry(
        string "Severity"
        variant                   string
"xyz.openbmc_project.Logging.Entry.Level.Warning"
    )
    dict entry(
        string "Message"
        variant                   string
"org.open_power.Logging.Error.TestError1"
    )
    dict entry(
        string "EventId"
        variant                   string ""
    )
    dict entry(
        string "AdditionalData"
        variant                   array [
                string "DEVICE_NAME=GPU_SXM_8"
                string "EVENT_NAME=PCIe Link Width State Change"
                string "RECOVERY_TYPE=property_change"
                string "REDFISH_MESSAGE_ARGS=GPU_SXM_8 PCIe, Abnormal Width
Change" string "REDFISH_MESSAGE_ID=ResourceEvent.1.0.ResourceErrorsDetected"
                string
"REDFISH_ORIGIN_OF_CONDITION=/xyz/openbmc_project/inventory/system/chassis/HGX_PCIeRetimer_7"
                string "namespace=GPU_SXM_8"
            ]
    )
    dict entry(
        string "Resolution"
        variant                   string ""
    )
    dict entry(
        string "Resolved"
        variant                   boolean false
    )
    dict entry(
        string "ServiceProviderNotify"
        variant                   boolean false
    )
    dict entry(
        string "UpdateTimestamp"
        variant                   uint64 1578862245336
    )
*/

/** @brief Method to parse event metadata */
void EventSignalHandler::eventParser(eventPropertiesType& eventProperties)
{
    std::time_t eventTimestamp = std::time(nullptr);
    std::string deviceName;
    std::string errorMessage;
    std::string severity;
    std::string redfishMessageId;
    std::string errorMessageDetails;
    EventRecord record;

    for (const auto& eventProperty : eventProperties)
    {
        // Process only event metadata
        if (eventProperty.first == "xyz.openbmc_project.Logging.Entry")
        {
            for (const auto& eventData : eventProperty.second)
            {
                if (eventData.first == "Severity")
                {
                    auto severityPtr =
                        std::get_if<std::string>(&eventData.second);
                    if (severityPtr)
                    {
                        severity = *(severityPtr);
                    }
                }
                // Process additional data — handle both old (as: "KEY=VALUE"
                // string array) and new (a{ss}: dict) phosphor-logging formats
                if (eventData.first == "AdditionalData")
                {
                    // Build unified KEY=VALUE string list from AdditionalData.
                    // phosphor-logging uses a{ss} (unordered_map) on D-Bus.
                    std::vector<std::string> msgStrings;

                    // Type-safe variant access — independent of variant member
                    // order, so a phosphor-logging variant reorder doesn't
                    // silently drop AdditionalData parsing.
                    using DictT = std::unordered_map<std::string, std::string>;
                    using VecT = std::vector<std::string>;
                    if (const auto* dict =
                            std::get_if<DictT>(&eventData.second))
                    {
                        for (const auto& [k, v] : *dict)
                        {
                            msgStrings.push_back(k + "=" + v);
                        }
                    }
                    else if (const auto* vec =
                                 std::get_if<VecT>(&eventData.second))
                    {
                        // Old format: vector<string> ("KEY=VALUE")
                        msgStrings = *vec;
                    }

                    if (msgStrings.empty())
                    {
                        return;
                    }
                    // Fetch device name and error message details
                    std::string errorOriginOfCondition;
                    std::string errorAdditionalInfo;
                    for (const std::string& msgString : msgStrings)
                    {
                        // REDFISH_MESSAGE_ID - message type identifier
                        if (msgString.find("REDFISH_MESSAGE_ID") !=
                            std::string::npos)
                        {
                            std::size_t equalSignPos = msgString.find('=');
                            redfishMessageId =
                                (equalSignPos != std::string::npos)
                                    ? msgString.substr(equalSignPos + 1)
                                    : "";
                        }
                        // Device name
                        if (msgString.find("DEVICE_NAME") != std::string::npos)
                        {
                            std::size_t equalSignPos = msgString.find('=');
                            deviceName =
                                (equalSignPos != std::string::npos)
                                    ? msgString.substr(equalSignPos + 1)
                                    : "";
                        }
                        // Event name - error message
                        if (msgString.find("EVENT_NAME") != std::string::npos)
                        {
                            std::size_t equalSignPos = msgString.find('=');
                            errorMessage =
                                (equalSignPos != std::string::npos)
                                    ? msgString.substr(equalSignPos + 1)
                                    : "";
                        }
                        // REDFISH_MESSAGE_ARGS - error message details
                        if (msgString.find("REDFISH_MESSAGE_ARGS") !=
                            std::string::npos)
                        {
                            std::size_t equalSignPos = msgString.find('=');
                            errorMessageDetails =
                                (equalSignPos != std::string::npos)
                                    ? msgString.substr(equalSignPos + 1)
                                    : "";
                        }
                        // REDFISH_ORIGIN_OF_CONDITION - device where error
                        // occurred
                        if (msgString.find("REDFISH_ORIGIN_OF_CONDITION") !=
                            std::string::npos)
                        {
                            std::size_t equalSignPos = msgString.find('=');
                            errorOriginOfCondition =
                                (equalSignPos != std::string::npos)
                                    ? msgString.substr(equalSignPos + 1)
                                    : "";
                        }
                        // DEVICE_EVENT_DATA  - error message detailed info
                        if (msgString.find("DEVICE_EVENT_DATA ") !=
                            std::string::npos)
                        {
                            std::size_t equalSignPos = msgString.find('=');
                            errorAdditionalInfo =
                                (equalSignPos != std::string::npos)
                                    ? msgString.substr(equalSignPos + 1)
                                    : "";
                        }
                    }

                    // Device name resolution: DEVICE_NAME from AdditionalData
                    // may be missing (vr-nvl-hmc) or generic without instance
                    // number (hgxb/hgxb300/hgxr set ImpactedComponent="GPU").
                    // If deviceName has no digit, it's not device-specific
                    // enough — fall through to extraction from MessageArgs.
                    //
                    // Strategy 1: REDFISH_MESSAGE_ARGS first field.
                    //   Entity-manager template: "GPU_$N Driver Event Message"
                    //   E.g., "GPU_0 Driver Event Message,..." → "GPU_0"
                    //         "GPU_SXM_1 Driver Event Message,..." →
                    //         "GPU_SXM_1"
                    //
                    // Strategy 2: REDFISH_ORIGIN_OF_CONDITION last path
                    // segment.
                    //   E.g., "/redfish/v1/Chassis/HGX_GPU_0" → "GPU_0"
                    //         "/redfish/v1/Chassis/HGX_GPU_SXM_1" → "GPU_SXM_1"
                    bool hasInstanceNumber = !deviceName.empty() &&
                                             std::any_of(deviceName.begin(),
                                                         deviceName.end(),
                                                         [](char c) {
                        return std::isdigit(static_cast<unsigned char>(c));
                    });
                    if (!hasInstanceNumber && !errorMessageDetails.empty())
                    {
                        // Strategy 1: first word of first comma-separated arg
                        auto commaPos = errorMessageDetails.find(',');
                        std::string firstArg =
                            (commaPos != std::string::npos)
                                ? errorMessageDetails.substr(0, commaPos)
                                : errorMessageDetails;
                        auto spacePos = firstArg.find(' ');
                        if (spacePos != std::string::npos)
                        {
                            deviceName = firstArg.substr(0, spacePos);
                        }
                    }

                    // Re-check: Strategy 1 may have set deviceName
                    hasInstanceNumber = !deviceName.empty() &&
                                        std::any_of(deviceName.begin(),
                                                    deviceName.end(),
                                                    [](char c) {
                        return std::isdigit(static_cast<unsigned char>(c));
                    });
                    if (!hasInstanceNumber && !errorOriginOfCondition.empty())
                    {
                        // Strategy 2: last segment of origin path
                        auto lastSlash =
                            errorOriginOfCondition.find_last_of('/');
                        if (lastSlash != std::string::npos)
                        {
                            deviceName =
                                errorOriginOfCondition.substr(lastSlash + 1);
                            // Strip "HGX_" prefix if present
                            // (e.g., "HGX_GPU_0" → "GPU_0")
                            if (deviceName.substr(0, 4) == "HGX_")
                            {
                                deviceName = deviceName.substr(4);
                            }
                        }
                    }

                    // Update event message to FDR store
                    auto fdrDeviceName = getFDRDeviceName(deviceName);
                    auto it = this->fdrDeviceEventsWriter.find(fdrDeviceName);
                    if (it != this->fdrDeviceEventsWriter.end() &&
                        !fdrDeviceName.empty())
                    {
                        // Object having FDR store writer and book of errors
                        // record
                        auto fdrDeviceEventRec = it->second;

                        // Store event data into FDR records - FDR store writer
                        auto fdrStoreWriter = fdrDeviceEventRec.first;
                        record = fdrDeviceEventRec.second;

                        // Write descriptive event details data into new single
                        // file Filepath
                        // BootCount_<id>_DateStamp_<fdr_timestamp>/GPU/GPU<id>/Event_<event_timestamp>.dat
                        std::shared_ptr<FDRStore> eventFDRStoreObj;
                        std::stringstream timeStampString;
                        timeStampString << eventTimestamp;

                        fdr->CreateSamplesWriter(
                            *record.profile, record.sectionID,
                            record.componentID, "FAULTS", ".dat",
                            eventFDRStoreObj, timeStampString.str());

                        // Create event details data protobuf message
                        fdr_event_details_data.set_eventtimestamp(
                            eventTimestamp);
                        fdr_event_details_data.set_eventname(errorMessage);
                        fdr_event_details_data.set_eventdevicename(deviceName);
                        fdr_event_details_data.set_eventmessage(
                            errorMessageDetails);
                        fdr_event_details_data.set_eventoriginofcondition(
                            errorOriginOfCondition);
                        fdr_event_details_data.set_eventadditionalinfo(
                            errorAdditionalInfo);
                        // Write event details to fdr space
                        eventFDRStoreObj->append(fdr_event_details_data);

                        // Add entry for event details log to Error.dat
                        auto filePath = eventFDRStoreObj->getStoreFilePath();
                        // remove the prefix "/var/emmc/fdr/" from the filePath
                        // as this prefix is valid only within the HMC. But on
                        // FDR dump, this is invalid.
                        size_t SubstrIndex = filePath.find("BootCount");
                        if (SubstrIndex != std::string::npos)
                        {
                            filePath = filePath.substr(SubstrIndex);
                        }
                        // Create protobuf message
                        fdr_event_data.set_eventtimestamp(eventTimestamp);
                        fdr_event_data.set_eventname(errorMessage);
                        fdr_event_data.set_eventloggingpath(filePath);
                        // Write to fdr space
                        fdrStoreWriter->append(fdr_event_data);
                    }
                }
            }
            break; // Skip processing other elements
        }
    }

    // Forward event to DeviceDumpHandler for dump collection
    if (fdr->deviceDumpHandler && !deviceName.empty())
    {
        fdr->deviceDumpHandler->onEvent(deviceName, redfishMessageId,
                                        errorMessageDetails, severity);
    }

    if (!deviceName.empty() && !errorMessage.empty() &&
        (severity == "xyz.openbmc_project.Logging.Entry.Level.Critical" ||
         severity == "xyz.openbmc_project.Logging.Entry.Level.Warning"))
    {
        fdrlog::info(
            "Triggering book of error condition for deviceName: {}; error : {};",
            deviceName, errorMessage);
        // Add book of errors record
        PropertyVariant val = std::string(""); // No value associated
        // Use infoID as 'FAULTS'
        // Use paramID as default 9999 - No params
        if (!record.componentID.empty())
        {
            fdr->BookOfErrorEngine("FAULTS", 9999, record.componentID,
                                   eventTimestamp, val);
        }
        else
        {
            fdr->BookOfErrorEngine("FAULTS", 9999, deviceName, eventTimestamp,
                                   val);
        }
    }
}

/** @brief Method to register callback for event create signals. */
void EventSignalHandler::registerEventsSignal()
{
    // Callback function for event signals
    auto interfacesAddedHandler = [this](sdbusplus::message_t& m) {
        // Process the event message
        sdbusplus::message::object_path objPath;
        eventPropertiesType eventProperties;

        fdrlog::debug("InterfacesAdded signal callback entered");
        try
        {
            m.read(objPath, eventProperties);
            fdrlog::debug("Event signal received: path={}, interfaces={}",
                          objPath.str, eventProperties.size());

            this->eventParser(eventProperties);
        }
        catch (const sdbusplus::exception_t& e)
        {
            fdrlog::error("D-Bus exception on event message read: {} (name={})",
                          e.what(), e.name());
        }
        catch (const std::exception& e)
        {
            fdrlog::error("Caught exception on event message read: {}",
                          e.what());
        }
    };

    // Get DBus connection
    auto& bus = getBus();

    // Add event interface added signal watch
    eventHandlerMatcher = std::make_unique<sdbusplus::bus::match_t>(
        bus,
        std::string("type='signal',member='") + eventMember +
            std::string("',interface='") + eventIface +
            std::string("',path='") + eventObjPath + std::string("'"),
        std::move(interfacesAddedHandler));
}

namespace rfEvent
{

void createLogEntry(const std::string& messageId,
                    const std::vector<std::string>& messageArgs,
                    const std::string& severity, const std::string& resolution,
                    const std::string& name, sdbusplus::bus_t* busPtr)
{
    std::map<std::string, std::string> addData;
    addData["REDFISH_MESSAGE_ID"] = messageId;
    if (!messageArgs.empty())
    {
        std::ostringstream oss;
        for (size_t i = 0; i < messageArgs.size(); ++i)
        {
            if (i > 0)
                oss << ",";
            oss << messageArgs[i];
        }
        addData["REDFISH_MESSAGE_ARGS"] = oss.str();
    }
    addData["xyz.openbmc_project.Logging.Entry.Resolution"] = resolution;
    addData["Name"] = name;

    try
    {
        if (busPtr)
        {
            auto method = busPtr->new_method_call(
                "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
                "xyz.openbmc_project.Logging.Create", "Create");
            method.append(messageId);
            method.append(severity);
            method.append(addData);
            busPtr->call_noreply(method);
        }
        else
        {
            auto localBus = sdbusplus::bus::new_default();
            auto method = localBus.new_method_call(
                "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
                "xyz.openbmc_project.Logging.Create", "Create");
            method.append(messageId);
            method.append(severity);
            method.append(addData);
            localBus.call_noreply(method);
        }
    }
    catch (const std::exception& e)
    {
        fdrlog::error("Failed to create Redfish event log: {}", e.what());
    }
}
} // namespace rfEvent
