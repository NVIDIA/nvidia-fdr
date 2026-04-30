/*
 Copyright (c) 2026, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#pragma once

#include "fdr_logs_schema.pb.h"

#include "fdr_device_dump_store.hpp"
#include "fdr_policy.hpp"
#include "fdr_store.hpp"
#include "fdr_utils.hpp"

#include <sdeventplus/clock.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/utility/timer.hpp>

#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Timer type used for per-dump async polling and retry countdown.
// Same underlying type as the global FDR Timer alias in fdr.hpp.
using DumpTimer = sdeventplus::utility::Timer<sdeventplus::ClockId::Monotonic>;

/**
 * @brief State machine states for an in-flight dump collection.
 *
 * AwaitingGetDiag — context created; GetDiagnostics not yet called.
 *                   Transitions immediately in collectDump() before
 *                   the timer fires.
 * RetryWaiting    — GetDiagnostics returned Unavailable; counting down
 *                   retryWaitRemaining 1-second ticks before retrying.
 * PollingStatus   — GetDiagnostics succeeded; polling NSMD async Status
 *                   every 1 second until complete or timed out.
 */
enum class DumpState
{
    AwaitingGetDiag,
    RetryWaiting,
    PollingStatus
};

/**
 * @brief Per-in-flight dump context.
 *
 * Owned exclusively by DeviceDumpHandler::inFlightContexts.
 * Lifetime: from collectDump() → cleanupContext().
 */
struct DumpContext
{
    DeviceDumpProfile_t dumpProfile; ///< Copied from PPF at collection time
    Component_t component;           ///< Copied from PPF at collection time
    uint32_t eventCode{0};
    std::string eventMessage;
    std::time_t eventTimestamp{0};
    std::string tempFilePath;         ///< Temp write path (renamed on success)
    std::string key;                  ///< makeKey(profileName, componentId)
    int fd{-1};                       ///< Open fd given to NSMD GetDiagnostics
    std::string asyncObjPath;         ///< D-Bus path returned by GetDiagnostics
    DumpState state{DumpState::AwaitingGetDiag};
    int attempt{0};                   ///< GetDiagnostics calls made (1-indexed)
    int retryWaitRemaining{0};        ///< 1-second ticks until next retry
    int elapsedPollSecs{0};           ///< Seconds spent polling Status
    std::unique_ptr<DumpTimer> timer; ///< 1-second repeating event-loop timer
};

/**
 * @brief Generic Device Dump Collection Engine.
 *
 * DeviceDumpHandler is driven by DeviceDumpProfile entries in the PPF YAML.
 * It listens for phosphor-logging InterfacesAdded events (via onEvent()),
 * matches them against registered DumpProfiles, and fetches device dump data
 * from the provider (e.g., NSMD GetDiagnostics when the profile is GpuDump).
 *
 * Unlike the previous detached-thread implementation, all D-Bus calls and
 * state transitions run on the FDR sdeventplus event loop:
 *  - GetDiagnostics is called synchronously from the event loop thread
 *    (blocking call returns quickly with an async operation path).
 *  - A 1-second repeating DumpTimer drives the async Status polling loop.
 *  - On Unavailable errors, a retry countdown runs within the same timer.
 *
 * Each dump is wrapped in an fdr_device_dump protobuf envelope and stored
 * to eMMC. FIFO eviction ensures bounded storage per device per profile.
 */
class DeviceDumpHandler
{
  private:
    Profile_t& profile;
    sdeventplus::Event& sdEvent; ///< FDR event loop (FdrEvents from fdr.hpp)

    // Per-device, per-profile debounce tracking
    // Key: "{ProfileName}:{ComponentID}"
    std::map<std::string, std::time_t> debounceMap;

    // In-flight collection contexts (replaces old inFlightSet).
    // Key: "{ProfileName}:{ComponentID}"
    std::map<std::string, std::unique_ptr<DumpContext>> inFlightContexts;

    // Storage engine: handles eMMC lifecycle for all device dump profiles.
    DeviceDumpStore dumpStore_;

    /**
     * @brief Check if event matches a DumpProfile's trigger criteria.
     * @return true if match, outCode is set to parsed event code.
     */
    bool matchEvent(const std::string& messageId,
                    const std::string& messageArgs,
                    const DeviceDumpProfile_t& dumpProfile, uint32_t& outCode);

    /**
     * @brief Parse event code from message args string.
     * E.g., "XID 119,GPU_SXM_1,..." with prefix "XID " -> 119
     */
    uint32_t parseEventCode(const std::string& messageArgs,
                            const std::string& prefix);

    /**
     * @brief Find the matching Component_t for a given device name.
     */
    Component_t* findComponent(const std::string& deviceName,
                               const DeviceDumpProfile_t& dumpProfile);

    /**
     * @brief Resolve D-Bus object path by substituting $param placeholders.
     */
    std::string resolveObjectPath(const DeviceDumpProfile_t& dumpProfile,
                                  const Component_t& component);

    /**
     * @brief Check if collection is within debounce window.
     * @return true if should proceed, false if debounced (skip).
     */
    bool debounceCheck(const std::string& profileName,
                       const std::string& componentId);

    /**
     * @brief Set up context and initiate dump collection.
     *
     * Creates the DumpContext, calls GetDiagnostics synchronously on the
     * event loop, then starts the 1-second polling timer if no hard error.
     */
    void collectDump(const DeviceDumpProfile_t& dumpProfile,
                     Component_t& component, uint32_t eventCode,
                     const std::string& eventMessage,
                     std::time_t eventTimestamp);

    /**
     * @brief Issue the GetDiagnostics D-Bus call for the given context key.
     *
     * Blocking call on the event loop thread (returns quickly — NSMD
     * responds immediately with the async operation path).
     *
     * Transitions context state to:
     *   PollingStatus   — on success (asyncObjPath set)
     *   RetryWaiting    — on Unavailable with retries remaining
     *
     * Calls cleanupContext() on hard error or retry exhaustion.
     */
    void callGetDiagnostics(const std::string& key);

    /**
     * @brief Create (or replace) the 1-second repeating DumpTimer for key.
     *
     * The timer fires tickContext() every second until the context is
     * cleaned up (which destroys the timer).
     */
    void startTimer(const std::string& key);

    /**
     * @brief Timer tick callback — runs the per-context state machine.
     *
     * RetryWaiting: decrements retryWaitRemaining; on expiry calls
     *               callGetDiagnostics().
     * PollingStatus: increments elapsedPollSecs; calls pollAsyncStatus().
     */
    void tickContext(const std::string& key);

    /**
     * @brief Poll com.nvidia.Async.Status for the in-flight async operation.
     *
     * Success  → close fd, call onDumpComplete(), call cleanupContext().
     * InProgress, within timeout → return (timer re-fires in 1 s).
     * InProgress, timed out / error → call cleanupContext().
     */
    void pollAsyncStatus(const std::string& key);

    /**
     * @brief Release all resources for an in-flight context.
     *
     * Closes the fd, removes the temp file (if still present), destroys
     * the DumpTimer, and erases the context from inFlightContexts.
     *
     * Safe to call from within the DumpTimer callback — sdeventplus /
     * sd-event hold a dispatch reference that prevents premature source
     * destruction.
     */
    void cleanupContext(const std::string& key);

    /**
     * @brief Handle completed dump retrieval — validate, wrap, store.
     *
     * Reads the raw dump from tempFilePath, wraps it in an
     * fdr_device_dump protobuf, then delegates to dumpStore_.store().
     *
     * Does NOT manage inFlightContexts — the caller (pollAsyncStatus)
     * is responsible for calling cleanupContext() after this returns.
     */
    void onDumpComplete(const DeviceDumpProfile_t& dumpProfile,
                        const Component_t& component, uint32_t eventCode,
                        const std::string& eventMessage,
                        std::time_t eventTimestamp,
                        const std::string& tempFilePath, uint32_t attemptCount);

    /**
     * @brief Build the debounce/index key for a profile+component pair.
     */
    std::string makeKey(const std::string& profileName,
                        const std::string& componentId);

  public:
    /**
     * @brief Construct DeviceDumpHandler from parsed PPF Profile.
     *
     * @param profile  FDR platform profile (from PPF YAML).
     * @param event    The FDR sdeventplus event loop (FdrEvents).
     *                 DumpTimers are registered against this event.
     */
    explicit DeviceDumpHandler(Profile_t& profile, sdeventplus::Event& event);

    /**
     * @brief Called from EventSignalHandler when an InterfacesAdded event
     * arrives. Matches against all registered DumpProfiles and triggers
     * collection if appropriate.
     */
    void onEvent(const std::string& deviceName, const std::string& messageId,
                 const std::string& messageArgs, const std::string& severity);

    /**
     * @brief Startup recovery scan for missed XIDs while FDR was down.
     * (Deferred to post-MVP — logs a placeholder message.)
     */
    void startupRecoveryScan();

  private:
    /**
     * @brief Record a terminal collection failure for operator visibility.
     *
     * Called only from terminal failure paths (not per-attempt retries).
     * Produces two outputs:
     *   1. Appends an fdr_device_dump_failure record to the shared
     *      cross-boot file at Bookkeeper/DeviceDumpFailures.dat, so the
     *      failure persists for HMC-lifetime queries (sibling of
     *      BookOfErrors.dat — survives BootCount_* directory cleanup).
     *      BootId field is the per-record boot key.
     *   2. Creates a phosphor-logging Entry (Severity=Error) so the
     *      failure appears in the BMC EventLog and Redfish clients see
     *      it live, with AdditionalData keys FDR_PROFILE / FDR_DEVICE /
     *      FDR_STAGE / FDR_XID / FDR_ATTEMPTS / FDR_ERROR.
     *
     * No lock: FDR is single-threaded (SADD FC-5); all call sites run
     * on the sdeventplus event loop, and the file is opened in
     * std::ios::app so each record append is atomic under PIPE_BUF.
     */
    void recordCollectionFailure(const std::string& profileName,
                                 const std::string& deviceId,
                                 uint32_t eventCode,
                                 const std::string& failureStage,
                                 const std::string& errorDetail,
                                 uint32_t attemptCount);

    /**
     * @brief Append a record to the cross-boot DeviceDumpRecords.dat index.
     *
     * Unified cross-boot index of every collection event — both successes
     * and failures. Written as length-delimited protobuf to
     * Bookkeeper/DeviceDumpRecords.dat alongside DeviceDumpFailures.dat.
     * Sibling of BookOfErrors.dat: same Bookkeeper/ home, same lifetime
     * semantics (survives BootCount_* directory cleanup), BootId field is
     * the per-record boot key. Lets offline tooling enumerate "all dumps
     * ever collected on this HMC, with outcome" from one file.
     *
     * For SUCCESS records, filePath is the stored dump's path relative to
     * LogsBasePath, sizeBytes is the payload size, and failureStage /
     * errorDetail are empty. For FAILED records, filePath / sizeBytes are
     * empty/zero, and failureStage / errorDetail mirror the values written
     * to DeviceDumpFailures.dat.
     *
     * No phosphor-logging Entry side-effect — that remains a failure-only
     * concern owned by recordCollectionFailure().
     */
    void appendDumpRecord(uint32_t status, const std::string& profileName,
                          const std::string& deviceId, uint32_t eventCode,
                          uint64_t eventTimeStamp, uint64_t completionTimeStamp,
                          const std::string& filePath, uint64_t sizeBytes,
                          uint32_t attemptCount,
                          const std::string& failureStage,
                          const std::string& errorDetail);
};
