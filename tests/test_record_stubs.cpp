/*
 * Extended stubs for fdr_record_test.
 * Provides no-op implementations for symbols referenced by fdr_record.cpp
 * that we do not exercise in the unit test (e.g. DBus accessors, global fdr
 * pointer, etc.).
 */

#include "fdr.hpp"
#include "fdr_redfish.hpp"

// Global FDR pointer expected by fdr_record.cpp (line 322: fdr->rfc)
FlightDataRecorder_c* fdr = nullptr;

// Static member of FlightDataRecorder_c
std::unordered_map<std::string, Record*>
    FlightDataRecorder_c::subscribedRecListMap;

// Configurable return values for DBus stubs — tests set these before calling
// Record::Refresh() to exercise different variant branches.
PropertyVariant g_stubDbusReturnValue{};
RetCoreApi g_stubDbusDGDReturnValue = std::make_tuple(0, std::string{},
                                                      uint64_t{0});
PassthroughFPGA g_stubDbusPTReturnValue = std::make_tuple(0, uint64_t{0});

namespace dbus
{
PropertyVariant readDbusProperty(const std::string& /*service*/,
                                 const std::string& /*objPath*/,
                                 const std::string& /*interface*/,
                                 const std::string& /*property*/,
                                 Info_t& /*info*/)
{
    return g_stubDbusReturnValue;
}

RetCoreApi readDbusDGDProperty(const std::string& /*service*/,
                               const std::string& /*objPath*/,
                               const std::string& /*interface*/,
                               const std::string& /*property*/,
                               const std::int64_t& /*devId*/)
{
    return g_stubDbusDGDReturnValue;
}

PassthroughFPGA readDbusPTProperty(const std::string& /*service*/,
                                   const std::string& /*objPath*/,
                                   const std::string& /*interface*/,
                                   const uint8_t& /*opcode*/,
                                   const std::uint8_t& /*arg1*/,
                                   const std::uint8_t& /*arg2*/)
{
    return g_stubDbusPTReturnValue;
}
} // namespace dbus

// RedfishClient stubs — fdr_record.cpp references these via fdr->rfc but
// tests never exercise the Redfish path (fdr is nullptr).
RedfishClient::RedfishClient(const std::string& /*prefix*/,
                             const std::string& /*user*/,
                             const std::string& /*password*/) : httpc(nullptr)
{}
RedfishClient::~RedfishClient() {}

std::string RedfishClient::query(const std::string& /*uri*/)
{
    return {};
}
std::string RedfishClient::query_string(const std::string& /*uri*/,
                                        const std::string& /*json_pointer*/)
{
    return {};
}
std::uint64_t RedfishClient::query_uint64t(const std::string& /*uri*/,
                                           const std::string& /*json_pointer*/)
{
    return 0;
}
std::int64_t RedfishClient::query_int64t(const std::string& /*uri*/,
                                         const std::string& /*json_pointer*/)
{
    return 0;
}
void RedfishClient::login() {}
void RedfishClient::logout() {}
nlohmann::json RedfishClient::query_json(const std::string& /*uri*/)
{
    return {};
}
