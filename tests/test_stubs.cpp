/*
 * Stub implementations for unit test linking.
 * Provides no-op replacements for heavy integration dependencies
 * (DBus, Redfish event logging, etc.) so that core logic can be
 * tested in isolation.
 */

#include <cstdint>
#include <string>
#include <vector>

// Globals declared extern in fdr_common.hpp, normally defined in fdr.cpp
std::string bootCounter = "0";
std::string sensorDirTimestamp = "20240101_000000";
std::string fdrHmcAlivePathName;
std::string CommonFdrKeepersDirName;
const uint32_t currentDataFormatVersion = 1;

#include "fdr_events.hpp"

EventSignalHandler::~EventSignalHandler() = default;

namespace rfEvent
{
void createLogEntry(const std::string& /*messageId*/,
                    const std::vector<std::string>& /*messageArgs*/,
                    const std::string& /*severity*/,
                    const std::string& /*resolution*/,
                    const std::string& /*name*/, sdbusplus::bus_t* /*busPtr*/)
{}
} // namespace rfEvent
