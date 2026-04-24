/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Mock DBus accessor for unit testing.
 *
 * The production dbus_accessor.hpp exposes free functions that call
 * sdbusplus::bus directly. This header provides:
 *
 *   1. An IDbusAccessor interface matching the production API
 *   2. A MockDbusAccessor using MOCK_METHOD for gtest/gmock
 *   3. No-op stub implementations of the dbus:: free functions
 *      so tests can link without a live D-Bus connection.
 *
 * Usage in tests:
 *   #include "testCommon.hpp"       // #define private public + gtest/gmock
 *   #include "mockDbusAccessor.hpp"
 *
 * Pattern follows nsmd/test/mockDBusHandler.hpp
 */

#pragma once

#include "property_variant.hpp"

#include <string>

#include <gmock/gmock.h>

namespace fdrtest
{

class IDbusAccessor
{
  public:
    virtual ~IDbusAccessor() = default;

    virtual std::string getService(const std::string& objectPath,
                                   const std::string& interface) = 0;

    virtual PropertyVariant readDbusProperty(const std::string& service,
                                             const std::string& objPath,
                                             const std::string& interface,
                                             const std::string& property,
                                             Info_t& info) = 0;

    virtual RetCoreApi readDbusDGDProperty(const std::string& service,
                                           const std::string& objPath,
                                           const std::string& interface,
                                           const std::string& property,
                                           const std::int64_t& devId) = 0;

    virtual PassthroughFPGA readDbusPTProperty(const std::string& service,
                                               const std::string& objPath,
                                               const std::string& interface,
                                               const std::uint8_t& opcode,
                                               const std::uint8_t& arg1,
                                               const std::uint8_t& arg2) = 0;

    virtual bool setDbusProperty(const std::string& service,
                                 const std::string& objPath,
                                 const std::string& interface,
                                 const std::string& property,
                                 const PropertyVariant& val) = 0;
};

class MockDbusAccessor : public IDbusAccessor
{
  public:
    MOCK_METHOD(std::string, getService,
                (const std::string& objectPath, const std::string& interface),
                (override));

    MOCK_METHOD(PropertyVariant, readDbusProperty,
                (const std::string& service, const std::string& objPath,
                 const std::string& interface, const std::string& property,
                 Info_t& info),
                (override));

    MOCK_METHOD(RetCoreApi, readDbusDGDProperty,
                (const std::string& service, const std::string& objPath,
                 const std::string& interface, const std::string& property,
                 const std::int64_t& devId),
                (override));

    MOCK_METHOD(PassthroughFPGA, readDbusPTProperty,
                (const std::string& service, const std::string& objPath,
                 const std::string& interface, const std::uint8_t& opcode,
                 const std::uint8_t& arg1, const std::uint8_t& arg2),
                (override));

    MOCK_METHOD(bool, setDbusProperty,
                (const std::string& service, const std::string& objPath,
                 const std::string& interface, const std::string& property,
                 const PropertyVariant& val),
                (override));
};

} // namespace fdrtest
