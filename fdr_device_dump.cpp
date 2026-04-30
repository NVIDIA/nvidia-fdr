/*
 Copyright (c) 2026, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include "fdr_device_dump.hpp"

#include "fdr_common.hpp"
#include "fdr_log.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <sdbusplus/bus.hpp>
#include <sdbusplus/message.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>

namespace fs = std::filesystem;

DeviceDumpHandler::DeviceDumpHandler(Profile_t& profile,
                                     sdeventplus::Event& event) :
    profile(profile), sdEvent(event), dumpStore_(profile)
{
    auto& profiles = profile.GeneralConfig.DeviceDumpProfiles;
    fdrlog::info("DeviceDumpHandler: initialized with {} dump profile(s)",
                 profiles.size());
    for (const auto& p : profiles)
    {
        fdrlog::info("  Profile: {} Section: {} BudgetMB: {} TriggerCodes: {}",
                     p.ProfileName, p.Section, p.Storage.BudgetMB,
                     p.EventSource.TriggerCodes.size());
    }

    dumpStore_.rebuildIndex();
}

std::string DeviceDumpHandler::makeKey(const std::string& profileName,
                                       const std::string& componentId)
{
    return profileName + ":" + componentId;
}

// ---------------------------------------------------------------------------
// Event matching
// ---------------------------------------------------------------------------

uint32_t DeviceDumpHandler::parseEventCode(const std::string& messageArgs,
                                           const std::string& prefix)
{
    auto pos = messageArgs.find(prefix);
    if (pos == std::string::npos)
    {
        return 0;
    }

    pos += prefix.length();
    try
    {
        auto parsed = std::stoul(messageArgs.substr(pos));
        if (parsed > std::numeric_limits<uint32_t>::max())
        {
            return 0;
        }
        return static_cast<uint32_t>(parsed);
    }
    catch (const std::exception&)
    {
        return 0;
    }
}

bool DeviceDumpHandler::matchEvent(const std::string& messageId,
                                   const std::string& messageArgs,
                                   const DeviceDumpProfile_t& dumpProfile,
                                   uint32_t& outCode)
{
    if (messageId != dumpProfile.EventSource.MessageIdPattern)
    {
        return false;
    }

    outCode = parseEventCode(messageArgs,
                             dumpProfile.EventSource.CodeParsePrefix);
    if (outCode == 0)
    {
        return false;
    }

    const auto& codes = dumpProfile.EventSource.TriggerCodes;
    return std::find(codes.begin(), codes.end(), outCode) != codes.end();
}

// ---------------------------------------------------------------------------
// Component resolution
// ---------------------------------------------------------------------------

Component_t*
    DeviceDumpHandler::findComponent(const std::string& deviceName,
                                     const DeviceDumpProfile_t& dumpProfile)
{
    auto lastUnderscore = deviceName.find_last_of('_');
    if (lastUnderscore == std::string::npos)
    {
        fdrlog::warn("DeviceDumpHandler: cannot parse device ID from '{}'",
                     deviceName);
        return nullptr;
    }
    std::string deviceIdStr = deviceName.substr(lastUnderscore + 1);

    // Resolve by matching the parsed device-id suffix against each Component's
    // "gpuid" Param. This is the same key used to substitute into
    // DeviceDumpProfile.Retrieval.ObjectPathTemplate (e.g. "…/GPU_$gpuid"),
    // so a match here guarantees the D-Bus path will resolve cleanly later.
    for (auto& section : profile.Sections)
    {
        if (section.ID != dumpProfile.Section)
        {
            continue;
        }

        for (auto& component : section.Components)
        {
            for (const auto& param : component.Params)
            {
                if (param.name == "gpuid" && param.value == deviceIdStr)
                {
                    return &component;
                }
            }
        }
    }

    fdrlog::warn(
        "DeviceDumpHandler: no matching component for device '{}' in section '{}'",
        deviceName, dumpProfile.Section);
    return nullptr;
}

std::string
    DeviceDumpHandler::resolveObjectPath(const DeviceDumpProfile_t& dumpProfile,
                                         const Component_t& component)
{
    std::string path = dumpProfile.Retrieval.ObjectPathTemplate;

    for (const auto& param : component.Params)
    {
        std::string placeholder = "$" + param.name;
        auto pos = path.find(placeholder);
        if (pos != std::string::npos)
        {
            path.replace(pos, placeholder.length(), param.value);
        }
    }

    return path;
}

// ---------------------------------------------------------------------------
// Debounce
// ---------------------------------------------------------------------------

bool DeviceDumpHandler::debounceCheck(const std::string& profileName,
                                      const std::string& componentId)
{
    for (const auto& p : profile.GeneralConfig.DeviceDumpProfiles)
    {
        if (p.ProfileName == profileName)
        {
            if (p.Debounce.WindowSecs <= 0)
            {
                return true;
            }

            auto key = makeKey(profileName, componentId);
            auto it = debounceMap.find(key);
            if (it == debounceMap.end())
            {
                return true;
            }

            auto now = std::time(nullptr);
            auto elapsed = now - it->second;
            if (elapsed >= p.Debounce.WindowSecs)
            {
                return true;
            }

            fdrlog::debug("DeviceDumpHandler: debounced {}:{} ({}s remaining)",
                          profileName, componentId,
                          p.Debounce.WindowSecs - elapsed);
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Timer-based dump collection — event loop path
// ---------------------------------------------------------------------------

void DeviceDumpHandler::collectDump(const DeviceDumpProfile_t& dumpProfile,
                                    Component_t& component, uint32_t eventCode,
                                    const std::string& eventMessage,
                                    std::time_t eventTimestamp)
{
    auto key = makeKey(dumpProfile.ProfileName, component.ID);

    if (inFlightContexts.count(key))
    {
        fdrlog::info("DeviceDumpHandler: collection already in-flight for {}",
                     key);
        return;
    }

    auto storagePath = dumpStore_.buildStoragePath(dumpProfile, component,
                                                   eventTimestamp);
    auto tempPath = storagePath + ".tmp";

    // Ensure the boot-count directory exists before opening the temp file.
    // CreateSamplesWriter creates it at startup for telemetry-producing
    // platforms, but a platform with only DeviceDumpProfiles (no telemetry)
    // reaches this path without the dir existing, and external cleanup
    // between startup and the XID could also remove it. Same pattern as
    // fdr.cpp::CreateSamplesWriter.
    try
    {
        fs::create_directories(fs::path(tempPath).parent_path());
    }
    catch (const std::exception& e)
    {
        fdrlog::error(
            "DeviceDumpHandler: failed to create dump directory for '{}': {}",
            tempPath, e.what());
        return;
    }

    int fd = open(tempPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        fdrlog::error(
            "DeviceDumpHandler: failed to create temp file '{}': errno={}",
            tempPath, errno);
        return;
    }

    auto ctx = std::make_unique<DumpContext>();
    ctx->dumpProfile = dumpProfile;
    ctx->component = component;
    ctx->eventCode = eventCode;
    ctx->eventMessage = eventMessage;
    ctx->eventTimestamp = eventTimestamp;
    ctx->tempFilePath = tempPath;
    ctx->key = key;
    ctx->fd = fd;

    inFlightContexts[key] = std::move(ctx);

    fdrlog::info("DeviceDumpHandler: initiating collection for {} XID {} — "
                 "context created, temp file '{}'",
                 key, eventCode, tempPath);

    // Issue GetDiagnostics synchronously on the event loop thread.
    // The call returns quickly — NSMD responds with an async operation path
    // and then writes dump data to fd asynchronously.
    callGetDiagnostics(key);

    // If context still exists (no hard error or retry exhaustion),
    // start the 1-second timer to drive the polling / retry state machine.
    if (inFlightContexts.count(key))
    {
        startTimer(key);
    }
}

void DeviceDumpHandler::callGetDiagnostics(const std::string& key)
{
    auto it = inFlightContexts.find(key);
    if (it == inFlightContexts.end())
    {
        return;
    }
    auto& ctx = *it->second;

    ctx.attempt++;
    const int maxRetries = ctx.dumpProfile.Retrieval.MaxRetries;
    const int retryDelayMs = ctx.dumpProfile.Retrieval.RetryDelayMs;
    auto objectPath = resolveObjectPath(ctx.dumpProfile, ctx.component);

    fdrlog::info(
        "DeviceDumpHandler: calling GetDiagnostics for {} (attempt {}/{})", key,
        ctx.attempt, maxRetries + 1);

    try
    {
        auto& bus = getBus();
        auto method = bus.new_method_call(
            ctx.dumpProfile.Retrieval.Service.c_str(), objectPath.c_str(),
            ctx.dumpProfile.Retrieval.Interface.c_str(),
            ctx.dumpProfile.Retrieval.Method.c_str());
        method.append(sdbusplus::message::unix_fd(ctx.fd));

        // Blocking call — returns quickly with the async operation path.
        // NSMD then writes dump data to fd asynchronously.
        auto reply = bus.call(
            method,
            std::chrono::microseconds(
                static_cast<uint64_t>(ctx.dumpProfile.Retrieval.TimeoutSecs) *
                1000000ULL));

        sdbusplus::message::object_path asyncObjPath;
        reply.read(asyncObjPath);
        ctx.asyncObjPath = asyncObjPath.str;
        ctx.state = DumpState::PollingStatus;
        ctx.elapsedPollSecs = 0;

        fdrlog::info(
            "DeviceDumpHandler: GetDiagnostics initiated for {}, async path: {}",
            key, ctx.asyncObjPath);
    }
    catch (const sdbusplus::exception_t& e)
    {
        std::string errMsg(e.what());
        bool isUnavailable = (errMsg.find("Unavailable") != std::string::npos);

        if (isUnavailable && ctx.attempt <= maxRetries)
        {
            // NSMD is busy (e.g., nsm-dump-tool is also calling
            // GetDiagnostics). Exponential backoff per SADD §4.6: base ×
            // 2^(attempt-1). Growth is bounded by MaxRetries, which already
            // terminates the retry loop; no separate wait-time cap needed.
            const int baseSecs = std::max(1, retryDelayMs / 1000);
            int waitSecs = baseSecs * (1 << (ctx.attempt - 1));
            fdrlog::warn("DeviceDumpHandler: {} Unavailable (attempt {}/{}), "
                         "retrying in {}s (exp backoff)",
                         key, ctx.attempt, maxRetries + 1, waitSecs);
            ctx.state = DumpState::RetryWaiting;
            ctx.retryWaitRemaining = waitSecs;
            // Caller starts the timer; it will call callGetDiagnostics again.
        }
        else
        {
            fdrlog::error("DeviceDumpHandler: GetDiagnostics failed for {} "
                          "after {} attempt(s): {}",
                          key, ctx.attempt, e.what());
            recordCollectionFailure(
                ctx.dumpProfile.ProfileName, ctx.component.ID, ctx.eventCode,
                "GetDiagnostics", e.what(), static_cast<uint32_t>(ctx.attempt));
            cleanupContext(key); // DO NOT access ctx after this
        }
    }
    catch (const std::exception& e)
    {
        fdrlog::error(
            "DeviceDumpHandler: unexpected error calling GetDiagnostics "
            "for {}: {}",
            key, e.what());
        recordCollectionFailure(ctx.dumpProfile.ProfileName, ctx.component.ID,
                                ctx.eventCode, "GetDiagnostics", e.what(),
                                static_cast<uint32_t>(ctx.attempt));
        cleanupContext(key); // DO NOT access ctx after this
    }
}

void DeviceDumpHandler::startTimer(const std::string& key)
{
    auto it = inFlightContexts.find(key);
    if (it == inFlightContexts.end())
    {
        return;
    }
    auto& ctx = *it->second;

    // Capture key by value. Inside the lambda, make a local stack copy
    // before calling tickContext — safe even if the lambda's own closure is
    // destroyed mid-dispatch when cleanupContext() destroys the DumpTimer.
    ctx.timer = std::make_unique<DumpTimer>(sdEvent, [this, key](DumpTimer&) {
        std::string localKey = key;
        tickContext(localKey);
    }, std::chrono::seconds{1});
}

void DeviceDumpHandler::tickContext(const std::string& key)
{
    auto it = inFlightContexts.find(key);
    if (it == inFlightContexts.end())
    {
        return; // Context already cleaned up
    }
    auto& ctx = *it->second;

    if (ctx.state == DumpState::RetryWaiting)
    {
        if (--ctx.retryWaitRemaining > 0)
        {
            return; // Still counting down
        }
        // Countdown expired — retry GetDiagnostics now.
        // callGetDiagnostics may call cleanupContext; do not access ctx after.
        callGetDiagnostics(key);
    }
    else if (ctx.state == DumpState::PollingStatus)
    {
        ctx.elapsedPollSecs++;
        // pollAsyncStatus may call cleanupContext; do not access ctx after.
        pollAsyncStatus(key);
    }
}

void DeviceDumpHandler::pollAsyncStatus(const std::string& key)
{
    auto it = inFlightContexts.find(key);
    if (it == inFlightContexts.end())
    {
        return;
    }
    auto& ctx = *it->second;

    try
    {
        auto& bus = getBus();
        auto statusMethod = bus.new_method_call(
            ctx.dumpProfile.Retrieval.Service.c_str(), ctx.asyncObjPath.c_str(),
            "org.freedesktop.DBus.Properties", "Get");
        statusMethod.append("com.nvidia.Async.Status", "Status");

        auto statusReply = bus.call(statusMethod);
        std::variant<std::string> statusVar;
        statusReply.read(statusVar);
        auto response = std::get<std::string>(statusVar);

        if (response == "com.nvidia.Async.Status.AsyncOperationStatus.Success")
        {
            fdrlog::info("DeviceDumpHandler: async operation complete for {}",
                         key);
            close(ctx.fd);
            ctx.fd = -1;
            // onDumpComplete reads tempFilePath and renames it to finalPath.
            // cleanupContext afterwards tries fs::remove(tempFilePath) which
            // will fail silently (file already renamed) — that is expected.
            onDumpComplete(ctx.dumpProfile, ctx.component, ctx.eventCode,
                           ctx.eventMessage, ctx.eventTimestamp,
                           ctx.tempFilePath,
                           static_cast<uint32_t>(ctx.attempt));
            cleanupContext(key); // DO NOT access ctx after this
        }
        else if (response ==
                 "com.nvidia.Async.Status.AsyncOperationStatus.InProgress")
        {
            if (ctx.elapsedPollSecs >= ctx.dumpProfile.Retrieval.TimeoutSecs)
            {
                fdrlog::error(
                    "DeviceDumpHandler: async operation timed out for {}", key);
                recordCollectionFailure(
                    ctx.dumpProfile.ProfileName, ctx.component.ID,
                    ctx.eventCode, "AsyncStatus",
                    "async operation timed out after " +
                        std::to_string(ctx.dumpProfile.Retrieval.TimeoutSecs) +
                        "s",
                    static_cast<uint32_t>(ctx.attempt));
                cleanupContext(key);
            }
            // else: still in progress — timer fires again in 1 second
        }
        else
        {
            fdrlog::error("DeviceDumpHandler: async status error for {}: {}",
                          key, response);
            recordCollectionFailure(
                ctx.dumpProfile.ProfileName, ctx.component.ID, ctx.eventCode,
                "AsyncStatus", response, static_cast<uint32_t>(ctx.attempt));
            cleanupContext(key);
        }
    }
    catch (const std::exception& e)
    {
        fdrlog::warn("DeviceDumpHandler: status poll failed for {}: {}", key,
                     e.what());
        recordCollectionFailure(ctx.dumpProfile.ProfileName, ctx.component.ID,
                                ctx.eventCode, "AsyncStatus", e.what(),
                                static_cast<uint32_t>(ctx.attempt));
        cleanupContext(key);
    }
}

void DeviceDumpHandler::cleanupContext(const std::string& key)
{
    auto it = inFlightContexts.find(key);
    if (it == inFlightContexts.end())
    {
        return;
    }
    auto& ctx = *it->second;

    fdrlog::debug("DeviceDumpHandler: cleanup context for {}", key);

    if (ctx.fd >= 0)
    {
        close(ctx.fd);
        ctx.fd = -1;
    }
    // Remove temp file if still present (i.e., onDumpComplete did not rename
    // it, meaning collection failed or was aborted).
    try
    {
        fs::remove(ctx.tempFilePath);
    }
    catch (...)
    {}

    // Erase from map — destroys DumpContext, which destroys the DumpTimer.
    // Safe to call from within the DumpTimer callback: sdeventplus/sd-event
    // holds a dispatch reference that keeps the source alive until after the
    // callback returns.
    inFlightContexts.erase(it);
}

// ---------------------------------------------------------------------------
// Dump completion — read, wrap, store
// ---------------------------------------------------------------------------

void DeviceDumpHandler::onDumpComplete(const DeviceDumpProfile_t& dumpProfile,
                                       const Component_t& component,
                                       uint32_t eventCode,
                                       const std::string& eventMessage,
                                       std::time_t eventTimestamp,
                                       const std::string& tempFilePath,
                                       uint32_t attemptCount)
{
    auto key = makeKey(dumpProfile.ProfileName, component.ID);

    // Read the dump data from temp file (written by NSMD via the fd)
    std::vector<uint8_t> dumpData;
    try
    {
        std::ifstream file(tempFilePath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            fdrlog::error("DeviceDumpHandler: cannot open temp file '{}'",
                          tempFilePath);
            return;
        }

        auto size = file.tellg();
        if (size == std::streampos(-1))
        {
            // tellg() can fail on a corrupted fd or stat error; bail out
            // before resize() gets static_cast<size_t>(-1) = SIZE_MAX and
            // tries to allocate ~18 EB.
            fdrlog::error("DeviceDumpHandler: tellg() failed on temp file '{}'",
                          tempFilePath);
            return;
        }
        file.seekg(0, std::ios::beg);
        dumpData.resize(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(dumpData.data()),
                  static_cast<std::streamsize>(size));
    }
    catch (const std::exception& e)
    {
        fdrlog::error("DeviceDumpHandler: failed to read temp file '{}': {}",
                      tempFilePath, e.what());
        return;
    }

    auto dumpSize = dumpData.size();

    if (dumpSize == 0)
    {
        fdrlog::info("DeviceDumpHandler: empty dump for {} (0 bytes), skipping",
                     key);
        return;
    }

    // No size-bounds gating: firmware dump size grows with firmware revisions;
    // per-profile BudgetMB + FIFO eviction handles runaway sizes naturally.
    // The 0-byte case is handled above ("no crash data" — normal skip).

    // Build protobuf envelope.
    // ProfileName IS the dump-type identifier (e.g. "GpuDump", "PmuCrashDump").
    // No separate DumpType field exists in the PPF or in the dump record
    // schema — the formerly-redundant fdr_device_dump.DumpType has been
    // dropped (proto field 2 is now `reserved`).
    fdrpb::fdr_device_dump pbDump;
    pbDump.set_eventtimestamp(static_cast<uint64_t>(eventTimestamp));
    pbDump.set_eventcode(eventCode);
    pbDump.set_eventmessage(eventMessage);
    pbDump.set_deviceid(component.ID);
    pbDump.set_devicetype(dumpProfile.Section);
    pbDump.set_bootid(bootCounter.empty() ? "0" : bootCounter);
    pbDump.set_dumpversion(1);
    pbDump.set_rawdump(dumpData.data(), dumpData.size());
    pbDump.set_collectiontimestamp(static_cast<uint64_t>(std::time(nullptr)));
    pbDump.set_collectionstatus("SUCCESS");
    pbDump.set_profilename(dumpProfile.ProfileName);

    fdrlog::info("DeviceDumpHandler: collected dump for {} XID {} ({} bytes)",
                 component.ID, eventCode, dumpSize);

    // Delegate storage (atomic write + index update + eviction) to store.
    if (!dumpStore_.canAccept())
    {
        fdrlog::warn(
            "DeviceDumpHandler: DeviceDumpStore cannot accept dump for {}",
            key);
        recordCollectionFailure(
            dumpProfile.ProfileName, component.ID, eventCode, "DiskSpaceCheck",
            "partition rejected post-collection (see journal for detail)", 0);
        return;
    }

    if (!dumpStore_.store(dumpProfile.ProfileName, pbDump, dumpProfile,
                          component, eventTimestamp))
    {
        fdrlog::error("DeviceDumpHandler: failed to store dump for {}", key);
        recordCollectionFailure(
            dumpProfile.ProfileName, component.ID, eventCode,
            "StoreWriteFailed",
            "store() returned false (see journal for serialize/rename detail)",
            0);
        return;
    }

    // Append a SUCCESS row to the unified cross-boot index. Path is stored
    // relative to LogsBasePath so the record stays valid inside the FDR
    // tar (the absolute eMMC prefix is HMC-only). buildStoragePath returns
    // the same path the store just wrote to, so the two are guaranteed
    // consistent.
    {
        std::string fullPath =
            dumpStore_.buildStoragePath(dumpProfile, component, eventTimestamp);
        const std::string& base = profile.GeneralConfig.LogsBasePath;
        std::string relPath = fullPath;
        if (relPath.rfind(base, 0) == 0)
        {
            relPath.erase(0, base.size());
            // Strip a single leading '/' if present so the recorded path
            // is BootCount_*/... rather than /BootCount_*/...
            if (!relPath.empty() && relPath.front() == '/')
            {
                relPath.erase(0, 1);
            }
        }
        auto now = std::time(nullptr);
        appendDumpRecord(fdrpb::fdr_device_dump_record::SUCCESS,
                         dumpProfile.ProfileName, component.ID, eventCode,
                         static_cast<uint64_t>(eventTimestamp),
                         static_cast<uint64_t>(now), relPath,
                         static_cast<uint64_t>(dumpSize), attemptCount,
                         /*failureStage=*/"", /*errorDetail=*/"");
    }

    debounceMap[key] = std::time(nullptr);
}

// ---------------------------------------------------------------------------
// Main event entry point
// ---------------------------------------------------------------------------

void DeviceDumpHandler::onEvent(const std::string& deviceName,
                                const std::string& messageId,
                                const std::string& messageArgs,
                                const std::string& severity)
{
    if (severity.find("Critical") == std::string::npos &&
        severity.find("Warning") == std::string::npos)
    {
        return;
    }

    for (auto& dumpProfile : profile.GeneralConfig.DeviceDumpProfiles)
    {
        uint32_t eventCode = 0;
        if (!matchEvent(messageId, messageArgs, dumpProfile, eventCode))
        {
            continue;
        }

        fdrlog::info(
            "DeviceDumpHandler: matched profile '{}' for device '{}' XID {}",
            dumpProfile.ProfileName, deviceName, eventCode);

        auto* component = findComponent(deviceName, dumpProfile);
        if (!component)
        {
            fdrlog::warn("DeviceDumpHandler: unknown device '{}' — no matching "
                         "Component in Section '{}'",
                         deviceName, dumpProfile.Section);
            continue;
        }

        if (!debounceCheck(dumpProfile.ProfileName, component->ID))
        {
            continue;
        }

        // Pre-check partition-level disk space before triggering collection.
        // Per-profile BudgetMB is enforced by eviction in store(), not here.
        auto key = makeKey(dumpProfile.ProfileName, component->ID);
        if (!dumpStore_.canAccept())
        {
            fdrlog::warn(
                "DeviceDumpHandler: skipping collection for {}, storage full",
                key);
            recordCollectionFailure(
                dumpProfile.ProfileName, component->ID, eventCode,
                "DiskSpaceCheck",
                "partition pre-check rejected (see journal for detail)", 0);
            continue;
        }

        collectDump(dumpProfile, *component, eventCode, messageArgs,
                    std::time(nullptr));
    }
}

void DeviceDumpHandler::startupRecoveryScan()
{
    fdrlog::info(
        "DeviceDumpHandler: startup recovery scan (not yet implemented)");
}

// ---------------------------------------------------------------------------
// Collection-failure recorder
// ---------------------------------------------------------------------------

void DeviceDumpHandler::recordCollectionFailure(const std::string& profileName,
                                                const std::string& deviceId,
                                                uint32_t eventCode,
                                                const std::string& failureStage,
                                                const std::string& errorDetail,
                                                uint32_t attemptCount)
{
    const auto now = std::time(nullptr);

    // Canonical grep anchor for every terminal failure. Preceded by a
    // site-specific log with exception/context detail; this single structured
    // line is the one operators grep to enumerate failures.
    fdrlog::error(
        "DeviceDumpHandler: terminal failure — profile='{}' device='{}' "
        "stage='{}' xid={} attempts={} detail='{}'",
        profileName, deviceId, failureStage, eventCode, attemptCount,
        errorDetail);

    // Build the protobuf envelope.
    fdrpb::fdr_device_dump_failure rec;
    rec.set_eventtimestamp(static_cast<uint64_t>(now));
    rec.set_profilename(profileName);
    rec.set_deviceid(deviceId);
    rec.set_eventcode(eventCode);
    rec.set_failurestage(failureStage);
    rec.set_errordetail(errorDetail);
    rec.set_attemptcount(attemptCount);
    rec.set_bootid(bootCounter.empty() ? "0" : bootCounter);

    // 1. Append length-delimited record to the shared cross-boot failure
    //    file under Bookkeeper/. Single-threaded single-writer + std::ios::app
    //    + ~200 byte records well under PIPE_BUF = atomic appends without a
    //    lock. Records carry BootId so the boot is recoverable across boots
    //    even after BootCount_* directories are cleaned.
    try
    {
        auto dirPath = profile.GeneralConfig.LogsBasePath + "/" +
                       CommonFdrKeepersDirName;
        fs::create_directories(dirPath);
        auto filePath = dirPath + "/DeviceDumpFailures.dat";

        std::ofstream ofs(filePath, std::ios::binary | std::ios::app);
        if (ofs.is_open())
        {
            auto bytes = rec.SerializeAsString();
            // Varint32 length prefix so readers can frame records.
            uint32_t size = static_cast<uint32_t>(bytes.size());
            while (size > 0x7F)
            {
                ofs.put(static_cast<char>((size & 0x7F) | 0x80));
                size >>= 7;
            }
            ofs.put(static_cast<char>(size & 0x7F));
            ofs.write(bytes.data(), bytes.size());
        }
        else
        {
            fdrlog::warn("DeviceDumpHandler: cannot open failure file at {}",
                         filePath);
        }
    }
    catch (const std::exception& e)
    {
        // Swallow — don't fail the fail path.
        fdrlog::warn("DeviceDumpHandler: failure-file append threw: {}",
                     e.what());
    }

    // 2. Create a phosphor-logging Entry so the failure is visible in the
    //    BMC EventLog (Redfish /LogServices/EventLog/Entries).
    try
    {
        auto& bus = getBus();
        auto method = bus.new_method_call(
            "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
            "xyz.openbmc_project.Logging.Create", "Create");

        std::map<std::string, std::string> addData;
        addData["FDR_PROFILE"] = profileName;
        addData["FDR_DEVICE"] = deviceId;
        addData["FDR_STAGE"] = failureStage;
        addData["FDR_XID"] = std::to_string(eventCode);
        addData["FDR_ATTEMPTS"] = std::to_string(attemptCount);
        addData["FDR_ERROR"] = errorDetail;

        std::string message =
            "FDR device-dump collection failed: " + profileName + " / " +
            (deviceId.empty() ? "<no-device>" : deviceId) + " / " +
            failureStage;

        method.append(message);
        method.append(
            std::string("xyz.openbmc_project.Logging.Entry.Level.Error"));
        method.append(addData);
        bus.call_noreply(method);
    }
    catch (const std::exception& e)
    {
        fdrlog::warn("DeviceDumpHandler: phosphor-logging Create threw: {}",
                     e.what());
    }

    // Mirror to the unified cross-boot index. Failure-side appendDumpRecord
    // call lives here so a single recordCollectionFailure() call site
    // produces all three outputs (failure-file, phosphor Entry, records-
    // file). Success-side append is in onDumpComplete(), right after the
    // dump is durably stored.
    appendDumpRecord(fdrpb::fdr_device_dump_record::FAILED, profileName,
                     deviceId, eventCode, static_cast<uint64_t>(now),
                     static_cast<uint64_t>(now), /*filePath=*/"",
                     /*sizeBytes=*/0, attemptCount, failureStage, errorDetail);
}

void DeviceDumpHandler::appendDumpRecord(
    uint32_t status, const std::string& profileName,
    const std::string& deviceId, uint32_t eventCode, uint64_t eventTimeStamp,
    uint64_t completionTimeStamp, const std::string& filePath,
    uint64_t sizeBytes, uint32_t attemptCount, const std::string& failureStage,
    const std::string& errorDetail)
{
    fdrpb::fdr_device_dump_record rec;
    rec.set_eventtimestamp(eventTimeStamp);
    rec.set_completiontimestamp(completionTimeStamp);
    rec.set_profilename(profileName);
    rec.set_deviceid(deviceId);
    rec.set_eventcode(eventCode);
    rec.set_status(static_cast<fdrpb::fdr_device_dump_record::Status>(status));
    rec.set_filepath(filePath);
    rec.set_sizebytes(sizeBytes);
    rec.set_attemptcount(attemptCount);
    rec.set_failurestage(failureStage);
    rec.set_errordetail(errorDetail);
    rec.set_bootid(bootCounter.empty() ? "0" : bootCounter);

    try
    {
        auto dirPath = profile.GeneralConfig.LogsBasePath + "/" +
                       CommonFdrKeepersDirName;
        fs::create_directories(dirPath);
        auto recordPath = dirPath + "/DeviceDumpRecords.dat";

        std::ofstream ofs(recordPath, std::ios::binary | std::ios::app);
        if (ofs.is_open())
        {
            auto bytes = rec.SerializeAsString();
            // Varint32 length prefix — same framing as DeviceDumpFailures.dat
            // so a single decoder works for both files.
            uint32_t size = static_cast<uint32_t>(bytes.size());
            while (size > 0x7F)
            {
                ofs.put(static_cast<char>((size & 0x7F) | 0x80));
                size >>= 7;
            }
            ofs.put(static_cast<char>(size & 0x7F));
            ofs.write(bytes.data(), bytes.size());
        }
        else
        {
            fdrlog::warn("DeviceDumpHandler: cannot open records file at {}",
                         recordPath);
        }
    }
    catch (const std::exception& e)
    {
        // Swallow — index file is best-effort. The dump payload (success
        // case) and the canonical failure file (failure case) are already
        // persisted by the caller; failing to also write the index must
        // not abort the parent operation.
        fdrlog::warn("DeviceDumpHandler: records-file append threw: {}",
                     e.what());
    }
}
