/*
 * Extended stubs for fdr_record_test.
 * Provides no-op implementations for symbols referenced by fdr_record.cpp
 * that we do not exercise in the unit test (e.g. DBus accessors, global fdr
 * pointer, etc.).
 */

#include "fdr.hpp"

// Global FDR pointer expected by fdr_record.cpp (line 322: fdr->rfc)
FlightDataRecorder_c* fdr = nullptr;

// Static member of FlightDataRecorder_c
std::unordered_map<std::string, Record*>
    FlightDataRecorder_c::subscribedRecListMap;

// DBus accessors called by Record::Refresh() — not exercised in these tests
namespace dbus
{
PropertyVariant readDbusProperty(const std::string& /*service*/,
                                 const std::string& /*objPath*/,
                                 const std::string& /*interface*/,
                                 const std::string& /*property*/,
                                 Info_t& /*info*/)
{
    return PropertyVariant{};
}

RetCoreApi readDbusDGDProperty(const std::string& /*service*/,
                               const std::string& /*objPath*/,
                               const std::string& /*interface*/,
                               const std::string& /*property*/,
                               const std::int64_t& /*devId*/)
{
    return std::make_tuple(0, std::string{}, uint64_t{0});
}

PassthroughFPGA readDbusPTProperty(const std::string& /*service*/,
                                   const std::string& /*objPath*/,
                                   const std::string& /*interface*/,
                                   const uint8_t& /*opcode*/,
                                   const std::uint8_t& /*arg1*/,
                                   const std::uint8_t& /*arg2*/)
{
    return std::make_tuple(0, uint64_t{0});
}
} // namespace dbus
