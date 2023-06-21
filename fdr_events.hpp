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

#include <sdbusplus/bus/match.hpp>
#include "fdr_utils.hpp"

class EventSignalHandler
{
  private:
    std::string eventObjPath;
    std::string eventIface;
    std::string eventMember;
    std::unique_ptr<sdbusplus::bus::match_t> eventHandlerMatcher;

    using eventPropertiesType = std::vector<std::pair<
      std::string, std::vector<std::pair<
        std::string, std::variant<
          uint64_t, uint32_t, std::string, bool, std::vector<std::string>>>>>>;

    void eventParser(eventPropertiesType&);

  public:
    EventSignalHandler(
        std::string eventObjPath,
        std::string eventIface, std::string eventMember) :
        eventObjPath(eventObjPath),
        eventIface(eventIface), eventMember(eventMember)
    {}

    ~EventSignalHandler();

    void registerEventsSignal();
};