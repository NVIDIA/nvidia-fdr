/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include <iostream>
#include "fdr.hpp"
#include "property_variant.hpp"
#include "fdr_events.hpp"

/** @brief Helper to fetch device id from device name */
std::string getDeviceId(const std::string& deviceName)
{
    std::string deviceId;
    // Find the position of the last underscore
    std::size_t lastUnderscorePos = deviceName.find_last_of('_');
    if (lastUnderscorePos != std::string::npos) {
        // Extract the substring after the underscore
        deviceId = deviceName.substr(lastUnderscorePos + 1);
    }

    return deviceId;
}

// TODO: Update fdr device name to match HMC DAT device name
/** @brief Method to convert event device name to FDR device name */
std::string EventSignalHandler::getFDRDeviceName(
    std::string& deviceName)
{
    std::string fdrDeviceName;
    // Translate GPU_SXM_1 to GPU1
    if (deviceName.find("GPU") != std::string::npos)
    {
        auto deviceId = getDeviceId(deviceName);
        fdrDeviceName = "GPU" + deviceId;
    }
    // Translate NVSwitch_0 to NVSwitch0
    else if (deviceName.find("NVSwitch") != std::string::npos)
    {
        auto deviceId = getDeviceId(deviceName);
        fdrDeviceName = "NVSwitch" + deviceId;
    }
    // Translate HGX_Baseboard_0 to Baseboard0
    else if (deviceName.find("Baseboard") != std::string::npos)
    {
        auto deviceId = getDeviceId(deviceName);
        fdrDeviceName = "Baseboard" + deviceId;
    }
    // Default device will be Baseboard0
    else
    {
        fdrDeviceName = "Baseboard0";
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
        variant                   string "xyz.openbmc_project.Logging.Entry.Level.Warning"
    )
    dict entry(
        string "Message"
        variant                   string "org.open_power.Logging.Error.TestError1"
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
                string "REDFISH_MESSAGE_ARGS=GPU_SXM_8 PCIe, Abnormal Width Change"
                string "REDFISH_MESSAGE_ID=ResourceEvent.1.0.ResourceErrorsDetected"
                string "REDFISH_ORIGIN_OF_CONDITION=/xyz/openbmc_project/inventory/system/chassis/HGX_PCIeRetimer_7"
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

    for (const auto& eventProperty: eventProperties)
    {
        // Process only event metadata
        if (eventProperty.first == "xyz.openbmc_project.Logging.Entry")
        {
            for (const auto& eventData: eventProperty.second)
            {
                // Process additional data
                if (eventData.first == "AdditionalData")
                {
                    const std::vector<std::string>* msgStringsPtr =
                        std::get_if<std::vector<std::string>>(&eventData.second);
                    if (msgStringsPtr == nullptr)
                    {
                        std::cout << "Got empty event AdditionalData info" << std::endl;
                        return;
                    }
                    // Fetch device name and error message details
                    std::string deviceName;
                    std::string errorMessage;
                    std::string errorMessageDetails;
                    std::string errorOriginOfCondition;
                    std::string errorAdditionalInfo;
                    for (const std::string& msgString : *msgStringsPtr)
                    {
                        // Device name
                        if (msgString.find("DEVICE_NAME") != std::string::npos)
                        {
                            std::size_t equalSignPos = msgString.find('=');
                            deviceName = (equalSignPos != std::string::npos) ?
                                msgString.substr(equalSignPos + 1) : "";
                        }
                        // Event name - error message
                        if (msgString.find("EVENT_NAME") != std::string::npos)
                        {
                            std::size_t equalSignPos = msgString.find('=');
                            errorMessage = (equalSignPos != std::string::npos) ?
                                msgString.substr(equalSignPos + 1) : "";
                        }
                        // REDFISH_MESSAGE_ARGS - error message details
                        if (msgString.find("REDFISH_MESSAGE_ARGS") != std::string::npos)
                        {
                            std::size_t equalSignPos = msgString.find('=');
                            errorMessageDetails = (equalSignPos != std::string::npos) ?
                                msgString.substr(equalSignPos + 1) : "";
                        }
                        // REDFISH_ORIGIN_OF_CONDITION - device where error occurred
                        if (msgString.find("REDFISH_ORIGIN_OF_CONDITION") != std::string::npos)
                        {
                            std::size_t equalSignPos = msgString.find('=');
                            errorOriginOfCondition = (equalSignPos != std::string::npos) ?
                                msgString.substr(equalSignPos + 1) : "";
                        }
                        // DEVICE_EVENT_DATA  - error message detailed info
                        if (msgString.find("DEVICE_EVENT_DATA ") != std::string::npos)
                        {
                            std::size_t equalSignPos = msgString.find('=');
                            errorAdditionalInfo = (equalSignPos != std::string::npos) ?
                                msgString.substr(equalSignPos + 1) : "";
                        }
                    }

                    // Update event message to FDR store
                    auto fdrDeviceName = getFDRDeviceName(deviceName);
                    auto it = this->fdrDeviceEventsWriter.find(fdrDeviceName);
                    if (it != this->fdrDeviceEventsWriter.end())
                    {
                        // Object having FDR store writer and book of errors record
                        auto fdrDeviceEventRec = it->second;

                        // Store event data into FDR records - FDR store writer
                        auto fdrStoreWriter = fdrDeviceEventRec.first;
                        auto record = fdrDeviceEventRec.second;

                        // Write descriptive event details data into new single file
                        // Filepath BootCount_<id>_DateStamp_<fdr_timestamp>/GPU/GPU<id>/Event_<event_timestamp>.log
                        std::shared_ptr<FDRStore> eventFDRStoreObj;
                        std::stringstream timeStampString;
                        timeStampString << eventTimestamp;

			            fdr->CreateSamplesWriter(*record.profile, record.sectionID,
                            record.componentID, "FAULTS", ".log", eventFDRStoreObj,
                            timeStampString.str());

                        // Create event details data protobuf message
                        fdr_event_details_data.set_eventtimestamp(eventTimestamp);
                        fdr_event_details_data.set_eventname(errorMessage);
                        fdr_event_details_data.set_eventdevicename(deviceName);
                        fdr_event_details_data.set_eventmessage(errorMessageDetails);
                        fdr_event_details_data.set_eventoriginofcondition(
                            errorOriginOfCondition);
                        fdr_event_details_data.set_eventadditionalinfo(
                            errorAdditionalInfo);
                        // Write event details to fdr space
                        eventFDRStoreObj->append(fdr_event_details_data);

                        // Add entry for event details log to Error.log
                        auto filePath = eventFDRStoreObj->getStoreFilePath();
                        // Create protobuf message
                        fdr_event_data.set_eventtimestamp(eventTimestamp);
                        fdr_event_data.set_eventname(errorMessage);
                        fdr_event_data.set_eventloggingpath(filePath);
                        // Write to fdr space
                        fdrStoreWriter->append(fdr_event_data);

                        // Add book of errors record
                        PropertyVariant val = std::string(""); // No value associated
                        // Use infoID as 'FAULTS'
                        // Use paramID as default 9999 - No params
                        fdr->BookOfErrorEngine("FAULTS", 9999, record.sectionID,
                            record.componentID, record.infogroupID, eventTimestamp, val);
                    }
                    else
                    {
                        std::cout << "Event store got unkown device: " << fdrDeviceName << std::endl;
                    }
                    break; // Skip processing other elements
                }
            }
            break; // Skip processing other elements
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

        try
        {
            m.read(objPath, eventProperties);
            this->eventParser(eventProperties);
		}
        catch (const std::exception& e)
        {
            std::cout << "Caught exception on event message read:" << e.what() << std::endl;
		}
    };

    // Get DBus connection
    auto& bus = getBus();

    // Add event interface added signal watch
    eventHandlerMatcher = std::make_unique<sdbusplus::bus::match_t>(
        bus,
        std::string("type='signal',member='") + eventMember +
            std::string("',interface='") + eventIface +
            std::string("',path='") + eventObjPath +
            std::string("'"),
        std::move(interfacesAddedHandler));

}