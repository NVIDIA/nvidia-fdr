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

RetCoreApi readDbusDGDProperty(const std::string& service, const std::string& objPath, 
                                 const std::string& interface, const std::string& property, const std::int64_t& devId)
{

    constexpr auto accMode = 1;
    uint64_t value = 0;
    std::string valueStr = "";
    std::tuple<int, std::string, std::vector<uint32_t>> response;

    auto bus = sdbusplus::bus::new_default();
    try{
        auto method = bus.new_method_call(service.c_str(), objPath.c_str(),
                                          interface.c_str(), callName);

        method.append((int)devId);
        method.append(property);
        method.append(accMode);
        auto reply = bus.call(method);
        reply.read(response);
    }
    catch (const sdbusplus::exception::exception& e){
        printf("readDbusDGDProperty: Failed to get property: error = %s\n", e.what());
    }
    auto rc = std::get<int>(response);
    auto data = std::get<std::vector<uint32_t>>(response);

    if (rc != 0){
        std::cout << "readDbusDGDProperty: bad return: "
                  << "; objPath: " << objPath
                  << "; property: " << property
                  << "; DevId: " << devId
                  << std::endl;
    }
    else{
        auto data = std::get<std::vector<uint32_t>>(response);
        if (data.size() >= 2){
            // Per SMBPBI spec: data[0]:dataOut, data[1]:exDataOut
            value = ((uint64_t)data[1] << 32 | data[0]);
        }

        // msg example: "Baseboard GPU over temperature info : 0001"
        valueStr = std::get<std::string>(response);
    }

    return std::make_tuple(rc, valueStr, value);
}

PassthroughFPGA readDbusPTProperty(const std::string& service, const std::string& objPath, 
                                 const std::string& interface, const uint8_t& opcode,
                                 const std::uint8_t& arg1, const std::uint8_t& arg2)
{

    std::vector<uint32_t> dataIn;
    int deviceId = 0;

    std::tuple<int, std::vector<uint32_t>> response;
    std::vector<uint32_t> dataOut;
    int rc;
    uint64_t fpgavalue;

    auto bus = sdbusplus::bus::new_default();

    try{
        auto method = bus.new_method_call(service.c_str(), objPath.c_str(),
                                          interface.c_str(), "PassthroughFpga");
        method.append(deviceId);
        method.append(opcode);
        method.append(arg1);
        method.append(arg2);
        method.append(dataIn);
        auto reply = bus.call(method);
        reply.read(response);
        std::tie (rc, dataOut) = response;

    }
    catch (const sdbusplus::exception::exception& e){
        printf("readDbusPTProperty: Failed to get property: error = %s\n", e.what());
    }

    if (rc != 0){
        std::cout << "readDbusPTProperty: bad return: "
                  << "; objPath: " << objPath
                  << "; opcode: " << opcode
                  << "; arg1: " << arg1
                  << "; arg2: " << arg2
                  << std::endl;
    }
    else{
        if (dataOut.size() == 4){
            fpgavalue = ((uint64_t)dataOut[3] << 32 | (uint64_t)dataOut[2]);
            
        }
        else{
            std::cout << "readDbusPTProperty: PassthroughFpga: Unknown SMBPBI response: "
                    << dataOut.size()
                    << "; objPath: " << objPath
                    << "; opcode: " << opcode
                    << "; arg1: " << arg1
                    << "; arg2: " << arg2
                    << std::endl;
        }

    }

    return std::make_tuple(rc, fpgavalue);

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
