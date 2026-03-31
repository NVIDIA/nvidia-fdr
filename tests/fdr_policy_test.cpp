/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for fdr_policy.hpp — YAML decode specializations
 *
 * Covers all YAML::convert<T>::decode for:
 *   FingerPrint_t, Events_t, CommandParams_t, DbusParams_t, ShmemParams_t,
 *   RedfishParams_t, Param_t, Info_t, InfoGroup_t, Component_t, Section_t,
 *   Preconditions_t, GeneralConfig_t, Profile_t
 */

#include "fdr_policy.hpp"
#include "testCommon.hpp"

// --- FingerPrint_t ---

TEST(FdrPolicy, DecodeFingerPrintWithChecks)
{
    auto fp = YAML::Load(R"(
Checks:
  - "grep -q Ubuntu /etc/os-release"
  - "uname -r"
)")
                  .as<FingerPrint_t>();
    ASSERT_EQ(fp.Checks.size(), 2u);
    EXPECT_EQ(fp.Checks[0], "grep -q Ubuntu /etc/os-release");
    EXPECT_EQ(fp.Checks[1], "uname -r");
}

TEST(FdrPolicy, DecodeFingerPrintEmpty)
{
    auto fp = YAML::Load("SomeOtherKey: true").as<FingerPrint_t>();
    EXPECT_TRUE(fp.Checks.empty());
}

// --- Events_t ---

TEST(FdrPolicy, DecodeEventsFull)
{
    auto ev = YAML::Load(R"(
ObjectPath: /xyz/openbmc_project/logging
Interface: org.freedesktop.DBus.ObjectManager
Member: InterfacesAdded
)")
                  .as<Events_t>();
    EXPECT_EQ(ev.objectPath, "/xyz/openbmc_project/logging");
    EXPECT_EQ(ev.interface, "org.freedesktop.DBus.ObjectManager");
    EXPECT_EQ(ev.member, "InterfacesAdded");
}

TEST(FdrPolicy, DecodeEventsPartial)
{
    auto ev = YAML::Load("ObjectPath: /xyz/logging").as<Events_t>();
    EXPECT_EQ(ev.objectPath, "/xyz/logging");
    EXPECT_TRUE(ev.interface.empty());
    EXPECT_TRUE(ev.member.empty());
}

// --- CommandParams_t ---

TEST(FdrPolicy, DecodeCommandParams)
{
    auto cp = YAML::Load(R"(
Command: "nvidia-smi --query-gpu=name"
WorkingDir: "/tmp"
)")
                  .as<CommandParams_t>();
    EXPECT_EQ(cp.Command, "nvidia-smi --query-gpu=name");
    EXPECT_EQ(cp.WorkingDir, "/tmp");
}

TEST(FdrPolicy, DecodeCommandParamsOptionalDir)
{
    auto cp = YAML::Load(R"(Command: "hostname")").as<CommandParams_t>();
    EXPECT_EQ(cp.Command, "hostname");
    EXPECT_TRUE(cp.WorkingDir.empty());
}

// --- DbusParams_t ---

TEST(FdrPolicy, DecodeDbusParamsFull)
{
    auto dp = YAML::Load(R"(
Service: org.freedesktop.NetworkManager
ObjectPath: /org/freedesktop/NetworkManager/Devices/0
Interface: org.freedesktop.NetworkManager.Device.Statistics
Property: RxBytes
DevId: 42
Opcode: 1
Arg1: 2
Arg2: 3
)")
                  .as<DbusParams_t>();
    EXPECT_EQ(dp.Service, "org.freedesktop.NetworkManager");
    EXPECT_EQ(dp.Property, "RxBytes");
    EXPECT_EQ(dp.DevId, 42);
    EXPECT_EQ(dp.Opcode, 1);
    EXPECT_EQ(dp.Arg1, 2);
    EXPECT_EQ(dp.Arg2, 3);
}

TEST(FdrPolicy, DecodeDbusParamsMinimal)
{
    auto dp = YAML::Load("Property: Temperature").as<DbusParams_t>();
    EXPECT_EQ(dp.Property, "Temperature");
    EXPECT_TRUE(dp.Service.empty());
}

// --- ShmemParams_t ---

TEST(FdrPolicy, DecodeShmemParams)
{
    auto sp = YAML::Load(R"(
Key: gpu_temp_0
Namespace: GPU_SXM_0
)")
                  .as<ShmemParams_t>();
    EXPECT_EQ(sp.Key, "gpu_temp_0");
    EXPECT_EQ(sp.Namespace, "GPU_SXM_0");
}

// --- RedfishParams_t ---

TEST(FdrPolicy, DecodeRedfishParams)
{
    auto rp = YAML::Load(R"(
URI: /redfish/v1/Chassis/1/Thermal
JSONPointer: /Temperatures/0/ReadingCelsius
)")
                  .as<RedfishParams_t>();
    EXPECT_EQ(rp.URI, "/redfish/v1/Chassis/1/Thermal");
    EXPECT_EQ(rp.JSONPointer, "/Temperatures/0/ReadingCelsius");
}

// --- Param_t ---

TEST(FdrPolicy, DecodeParamString)
{
    auto p = YAML::Load(R"(
name: gpuid
value: "0"
)")
                 .as<Param_t>();
    EXPECT_EQ(p.name, "gpuid");
    EXPECT_EQ(p.value, "0");
}

TEST(FdrPolicy, DecodeParamVectorFallback)
{
    auto p = YAML::Load(R"(
name: logpath
value:
  - "/tmp/fdr/"
)")
                 .as<Param_t>();
    EXPECT_EQ(p.name, "logpath");
    EXPECT_EQ(p.value, "/tmp/fdr/");
}

// --- Info_t ---

TEST(FdrPolicy, DecodeInfoTCommandFetch)
{
    auto info = YAML::Load(R"(
ID: GPUTemp
ParamID: 42
DataType: Uint64
FetchMethod: Command
StorePolicy: EveryFetch
FetchFreqSecs: 5
StoreFreqSecs: 5
FetchType: Poll
CommandParams:
  Command: "nvidia-smi --query-gpu=temperature.gpu"
)")
                    .as<Info_t>();
    EXPECT_EQ(info.ID, "GPUTemp");
    EXPECT_EQ(info.ParamID, 42u);
    EXPECT_EQ(info.DataType, "Uint64");
    EXPECT_EQ(info.FetchMethod, "Command");
    EXPECT_EQ(info.StorePolicy, "EveryFetch");
    EXPECT_EQ(info.FetchFreqSecs, 5);
    EXPECT_EQ(info.FetchType, "Poll");
}

TEST(FdrPolicy, DecodeInfoTDbusFetch)
{
    auto info = YAML::Load(R"(
ID: RxBytes
ParamID: 10
DataType: Uint64
FetchMethod: DBUS
DbusParams:
  Service: org.freedesktop.NetworkManager
  ObjectPath: /org/freedesktop/NetworkManager/Devices/0
  Interface: org.freedesktop.NetworkManager.Device.Statistics
  Property: RxBytes
)")
                    .as<Info_t>();
    EXPECT_EQ(info.FetchMethod, "DBUS");
    EXPECT_EQ(info.DbusParams.Property, "RxBytes");
}

TEST(FdrPolicy, DecodeInfoTRedfishFetch)
{
    auto info = YAML::Load(R"(
ID: ChassisPower
ParamID: 20
DataType: Uint64
FetchMethod: Redfish
RedfishParams:
  URI: /redfish/v1/Chassis/1/Power
  JSONPointer: /PowerControl/0/PowerConsumedWatts
)")
                    .as<Info_t>();
    EXPECT_EQ(info.FetchMethod, "Redfish");
    EXPECT_EQ(info.RedfishParams.URI, "/redfish/v1/Chassis/1/Power");
}

TEST(FdrPolicy, DecodeInfoTShmemFetch)
{
    auto info = YAML::Load(R"(
ID: GpuTemp
ParamID: 30
DataType: Uint64
FetchMethod: Shmem
ShmemParams:
  Key: gpu_temp_0
  Namespace: GPU_SXM_0
)")
                    .as<Info_t>();
    EXPECT_EQ(info.FetchMethod, "Shmem");
    EXPECT_EQ(info.ShmemParams.Key, "gpu_temp_0");
}

TEST(FdrPolicy, DecodeInfoTMissingRequiredFieldThrows)
{
    EXPECT_THROW(YAML::Load(R"(
FetchMethod: Command
DataType: string
)")
                     .as<Info_t>(),
                 YAML::Exception);
}

// --- InfoGroup_t ---

TEST(FdrPolicy, DecodeInfoGroupT)
{
    auto ig = YAML::Load(R"(
ID: Sensor.Thermal
InfoList:
  - ID: GPUTemp
    ParamID: 0
    DataType: Uint64
  - ID: MemTemp
    ParamID: 1
    DataType: Uint64
RecordRetentionPolicy: Recreate
CompactionMethod: Average
CompactionFreqSecs: 3600
)")
                  .as<InfoGroup_t>();
    EXPECT_EQ(ig.ID, "Sensor.Thermal");
    ASSERT_EQ(ig.InfoList.size(), 2u);
    EXPECT_EQ(ig.RecordRetentionPolicy, "Recreate");
    EXPECT_EQ(ig.CompactionMethod, "Average");
    EXPECT_EQ(ig.CompactionFreqSecs, 3600);
    EXPECT_EQ(ig.LastCompactedAt, 0);
}

// --- Component_t ---

TEST(FdrPolicy, DecodeComponentT)
{
    auto comp = YAML::Load(R"(
ID: GPU0
Params:
  - name: gpuid
    value: "0"
InfoGroups:
  - ID: Inventory
    InfoList:
      - ID: Model
        ParamID: 0
        DataType: string
)")
                    .as<Component_t>();
    EXPECT_EQ(comp.ID, "GPU0");
    ASSERT_EQ(comp.Params.size(), 1u);
    ASSERT_EQ(comp.InfoGroups.size(), 1u);
}

TEST(FdrPolicy, DecodeComponentTNoParams)
{
    auto comp = YAML::Load(R"(
ID: NIC0
InfoGroups:
  - ID: Stats
    InfoList:
      - ID: RxBytes
        ParamID: 0
        DataType: Uint64
)")
                    .as<Component_t>();
    EXPECT_EQ(comp.ID, "NIC0");
    EXPECT_TRUE(comp.Params.empty());
}

// --- Section_t ---

TEST(FdrPolicy, DecodeSectionT)
{
    auto sec = YAML::Load(R"(
ID: GPU
Components:
  - ID: GPU0
    InfoGroups:
      - ID: Inventory
        InfoList:
          - ID: Model
            ParamID: 0
            DataType: string
  - ID: GPU1
    InfoGroups:
      - ID: Inventory
        InfoList:
          - ID: Model
            ParamID: 0
            DataType: string
)")
                   .as<Section_t>();
    EXPECT_EQ(sec.ID, "GPU");
    ASSERT_EQ(sec.Components.size(), 2u);
}

// --- Preconditions_t ---

TEST(FdrPolicy, DecodePreconditionsWithChecks)
{
    auto pc = YAML::Load(R"(
Threshold: 30
Checks:
  - ID: CheckMount
    CommandParams:
      Command: "df /tmp"
    CommandRetryPolicy: "Yes"
    ExitOnFailure: "No"
)")
                  .as<Preconditions_t>();
    EXPECT_EQ(pc.Threshold, 30u);
    ASSERT_EQ(pc.Checks.size(), 1u);
    EXPECT_EQ(pc.Checks[0].ID, "CheckMount");
}

TEST(FdrPolicy, DecodePreconditionsDefaultThreshold)
{
    auto pc = YAML::Load("Checks: []").as<Preconditions_t>();
    EXPECT_EQ(pc.Threshold, 60u);
    EXPECT_TRUE(pc.Checks.empty());
}

// --- GeneralConfig_t ---

TEST(FdrPolicy, DecodeGeneralConfigFull)
{
    auto gc = YAML::Load(R"(
LogsBasePath:
  - "/var/emmc/fdr/"
LogsFormat: BINARY
LoggingFileMaxSize: 2097152
LoggingFileNumber: 5
LoggingLevel: debug
RedfishSchema: "http://192.168.1.31"
RedfishUser: admin
RedfishPassword: secret
CompactionWindowSecs: 86400
CompactionSubWindowSecs: 1800
HiFiDataPreserveTimeSecs: 3600
PartitionThresoldCheckMB: 200
ExceptionAllowNumber: 256
ExceptionAllowRate: 1.0
Events:
  ObjectPath: /xyz/openbmc_project/logging
  Interface: org.freedesktop.DBus.ObjectManager
  Member: InterfacesAdded
)")
                  .as<GeneralConfig_t>();
    EXPECT_EQ(gc.LogsBasePath, "/var/emmc/fdr/");
    EXPECT_EQ(gc.LogsFormat, "BINARY");
    EXPECT_EQ(gc.LoggingFileMaxSize, 2097152u);
    EXPECT_EQ(gc.LoggingFileNumber, 5u);
    EXPECT_EQ(gc.LoggingLevel, "debug");
    EXPECT_EQ(gc.RedfishSchema, "http://192.168.1.31");
    EXPECT_EQ(gc.CompactionWindowSecs, 86400u);
    EXPECT_EQ(gc.ExceptionAllowNumber, 256);
    EXPECT_FLOAT_EQ(gc.ExceptionAllowRate, 1.0);
}

TEST(FdrPolicy, DecodeGeneralConfigDefaults)
{
    auto gc = YAML::Load(R"(
LogsBasePath:
  - "/tmp/fdr/"
LogsFormat: JSON
)")
                  .as<GeneralConfig_t>();
    EXPECT_EQ(gc.LogsBasePath, "/tmp/fdr/");
    EXPECT_EQ(gc.LogsFormat, "JSON");
    EXPECT_EQ(gc.LoggingFileMaxSize, 1048576u);
    EXPECT_EQ(gc.LoggingFileNumber, 3u);
    EXPECT_EQ(gc.LoggingLevel, "info");
    EXPECT_EQ(gc.PartitionThresoldCheckMB, 100u);
    EXPECT_EQ(gc.ExceptionAllowNumber, 128);
    EXPECT_FLOAT_EQ(gc.ExceptionAllowRate, 0.5);
}

// --- Profile_t (end-to-end) ---

TEST(FdrPolicy, DecodeProfileTEndToEnd)
{
    auto profile = YAML::Load(R"(
FingerPrint:
  Checks:
    - "grep -q Ubuntu /etc/os-release"
Preconditions:
  Threshold: 10
  Checks: []
GeneralConfig:
  LogsBasePath:
    - "/tmp/fdr/"
  LogsFormat: JSON
Sections:
  - ID: GPU
    Components:
      - ID: GPU0
        InfoGroups:
          - ID: Inventory
            InfoList:
              - ID: Model
                ParamID: 0
                DataType: string
)")
                       .as<Profile_t>();
    ASSERT_EQ(profile.FingerPrint.Checks.size(), 1u);
    EXPECT_EQ(profile.Preconditions.Threshold, 10u);
    EXPECT_EQ(profile.GeneralConfig.LogsFormat, "JSON");
    ASSERT_EQ(profile.Sections.size(), 1u);
    EXPECT_EQ(profile.Sections[0].ID, "GPU");
}
