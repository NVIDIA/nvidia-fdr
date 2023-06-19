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

/** @brief Method to register callback for event create signals. */
void EventSignalHandler::registerEventsSignal()
{
    // Callback function for event signals
    auto interfacesAddedHandler = [&](sdbusplus::message_t& m) {
      // TODO: Add handler code here
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