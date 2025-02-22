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
#include "fdr_common.hpp"
#include "property_variant.hpp"

#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/bus.hpp>

namespace dbus
{

constexpr auto freeDesktopInterface = "org.freedesktop.DBus.Properties";
constexpr auto getCall = "Get";
constexpr auto setCall = "Set";
constexpr auto callName = "DeviceGetData";

using DbusPropertyChangedHandler = std::unique_ptr<sdbusplus::bus::match_t>;
using CallbackFunction = sdbusplus::bus::match::match::callback_t;

/**
 * @brief register for receiving signals from Dbus PropertyChanged
 * @param bus       the bus type sdbusplus::bus::bus&
 * @param objectPath
 * @param interface
 * @param callback
 * @return
 */
DbusPropertyChangedHandler registerServicePropertyChanged(
    sdbusplus::bus::bus& bus, const std::string& objectPath,
    const std::string& interface, CallbackFunction callback);

/**
 * @brief returns the service assigned with objectPath and interface
 * @param objectPath
 * @param interface
 * @return service name
 */
std::string getService(const std::string& objectPath,
                       const std::string& interface);

/**
 * @brief getDbusProperty() gets the value from a property in DBUS
 * @param objPath
 * @param interface
 * @param property
 * @return the value based on std::variant
 */
PropertyVariant readDbusProperty(const std::string& service,
                                 const std::string& objPath,
                                 const std::string& interface,
                                 const std::string& property, Info_t& info);

RetCoreApi readDbusDGDProperty(const std::string& service,
                               const std::string& objPath,
                               const std::string& interface,
                               const std::string& property,
                               const std::int64_t& devId);

PassthroughFPGA
    readDbusPTProperty(const std::string& service, const std::string& objPath,
                       const std::string& interface, const uint8_t& opcode,
                       const std::uint8_t& arg1, const std::uint8_t& arg2);

/**
 * @brief setDbusProperty() sets a value for a Dbus property
 * @param service
 * @param objPath
 * @param interface
 * @param property
 * @param val the new value to be set
 * @return true if could set this the value from 'val', false otherwise
 */
bool setDbusProperty(const std::string& service, const std::string& objPath,
                     const std::string& interface, const std::string& property,
                     const PropertyVariant& val);

/**
 * @brief setDbusProperty() just an overload function that calls getService()
 *                          to get the service for objPath and interface
 * @param objPath
 * @param interface
 * @param property
 * @param val the new value to be set
 * @return true if could set this the value from 'val', false otherwise
 */
bool setDbusProperty(const std::string& objPath, const std::string& interface,
                     const std::string& property, const PropertyVariant& val);

} // namespace dbus
