/*
 Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include "dbus_accessor.hpp"

#include <sdbusplus/exception.hpp>

namespace dbus
{

std::string getService(const std::string& objectPath,
                       const std::string& interface)
{
    constexpr auto mapperBusBame = "xyz.openbmc_project.ObjectMapper";
    constexpr auto mapperObjectPath = "/xyz/openbmc_project/object_mapper";
    constexpr auto mapperInterface = "xyz.openbmc_project.ObjectMapper";

    std::string ret{""};
    std::vector<std::pair<std::string, std::vector<std::string>>> response;
    auto bus = sdbusplus::bus::new_default();
    try
    {
        auto method = bus.new_method_call(mapperBusBame, mapperObjectPath,
                                          mapperInterface, "GetObject");
        method.append(objectPath, std::vector<std::string>({interface}));
        auto reply = bus.call(method);
        reply.read(response);
        if (response.empty() == false)
        {
            ret = response.begin()->first;
        }
        else
        {
            printf("getService() Service not found for: %s %s\n", objectPath.c_str(), interface.c_str());
        }
    }
    catch (const sdbusplus::exception::exception& e)
    {
        printf("getService() DBus error: error = %s\n", e.what());
    }
    return ret;
}

PropertyVariant readDbusProperty(const std::string& service, const std::string& objPath, 
                                 const std::string& interface, const std::string& property)
{

    PropertyVariant value;
    auto bus = sdbusplus::bus::new_default();
    try
    {
        auto method = bus.new_method_call(service.c_str(), objPath.c_str(),
                                          freeDesktopInterface, getCall);
        method.append(interface, property);
        auto reply = bus.call(method);
        reply.read(value);
    }
    catch (const sdbusplus::exception::exception& e)
    {
        printf("readDbusProperty() Failed to get property: error = %s\n", e.what());
    }
    return value;
}

bool setDbusProperty(const std::string& service, const std::string& objPath,
                     const std::string& interface, const std::string& property,
                     const PropertyVariant& val)
{
    auto bus = sdbusplus::bus::new_default();
    bool ret = false;
    try
    {
        auto method = bus.new_method_call(service.c_str(), objPath.c_str(),
                                          freeDesktopInterface, setCall);
        method.append(interface, property, val);
        ret = true;
        if (!bus.call(method))
        {
            ret = false;
        }
    }
    catch (const sdbusplus::exception::exception& e)
    {
        printf("setDbusProperty() Failed to set property: error = %s\n", e.what());
    }
    return ret;
}

} // namespace dbus
