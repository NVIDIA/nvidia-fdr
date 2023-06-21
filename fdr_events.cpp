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
#include "fdr_events.hpp"

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
    for (const auto& eventProperty: eventProperties)
    {
        // Process only event metadata
        if (eventProperty.first == "xyz.openbmc_project.Logging.Entry")
        {
            for (const auto& eventData: eventProperty.second)
            {
                // Process only additional data
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