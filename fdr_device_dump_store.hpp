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

#include "fdr_policy.hpp"

#include <ctime>
#include <deque>
#include <map>
#include <string>

/**
 * @brief Persistent storage engine for device dumps.
 *
 * DeviceDumpStore is a separate shared class — analogous to FDRStore for
 * telemetry — that manages the eMMC lifecycle of device dump files:
 *   - canAccept():       disk space pre-check
 *   - store():           atomic write (.tmp → rename → .dat) + index update
 *                        + FIFO eviction enforcement
 *   - enforceEviction(): deletes oldest entries across all devices in the
 *                        profile until total bytes <= BudgetMB * 1 MiB
 *   - rebuildIndex():    startup scan to reconstruct in-memory index from disk
 *
 * All future device dump handlers (NVSwitchDumpHandler, PMUDumpHandler, ...)
 * share this single class for consistent storage policy and FIFO eviction.
 *
 * Key:   ProfileName (one FIFO queue per profile, across all devices).
 * Value: ordered deque of DumpEntry (oldest-first) — eviction pops front
 *        to remove the globally-oldest dump in the profile regardless of
 *        which device produced it.
 */
class DeviceDumpStore
{
  public:
    /// One entry in a profile's FIFO queue. Timestamp is extracted from the
    /// filename's final numeric segment; deque is sorted by timestamp so
    /// pop_front always removes the globally-oldest entry in that profile.
    struct DumpEntry
    {
        std::string filepath;
        std::time_t timestamp;
    };

    explicit DeviceDumpStore(Profile_t& profile);

    /**
     * @brief Pre-collection check: partition-level disk space.
     *
     * Returns true if it is safe to proceed with collection. Called by
     * DeviceDumpHandler before issuing GetDiagnostics. Does NOT consider
     * per-profile budget — BudgetMB is enforced by eviction on every store.
     */
    bool canAccept();

    /**
     * @brief Atomically write a dump, update the index, enforce eviction.
     *
     * Writes pbDump to a .tmp file then renames to the final .dat path.
     * After a successful write, enforceEviction() keeps the profile's total
     * bytes within BudgetMB by evicting the globally-oldest dump across all
     * devices in the profile.
     *
     * @return true on success, false if write/rename failed.
     */
    bool store(const std::string& profileName,
               const fdrpb::fdr_device_dump& pbDump,
               const DeviceDumpProfile_t& dumpProfile,
               const Component_t& component, std::time_t timestamp);

    /**
     * @brief Scan eMMC directories and rebuild the in-memory dump index.
     * Called once at startup from DeviceDumpHandler constructor.
     */
    void rebuildIndex();

    /**
     * @brief Build the storage directory + file path for a dump.
     *
     * Format:
     * {LogsBasePath}/{BootCountDir}/{Section}.{ComponentID}.{FilePrefix}_{timestamp}.dat
     * Follows FDR's telemetry naming convention (dot-separated) for
     * consistency with existing data files (e.g. GPU.HGX_GPU_0.sensors.dat).
     */
    std::string buildStoragePath(const DeviceDumpProfile_t& dumpProfile,
                                 const Component_t& component,
                                 std::time_t timestamp);

  private:
    Profile_t& profile_;

    /// Per-profile FIFO queue: all dumps for this profile across every
    /// device, ordered oldest-first. Eviction is globally-oldest within
    /// the profile — a flooding device can displace older dumps from
    /// other devices, by design (see SADD §2.2).
    std::map<std::string, std::deque<DumpEntry>> dumpIndex_;

    /**
     * @brief FIFO eviction: delete oldest entries across all devices in
     *        the profile until total bytes <= BudgetMB * 1 MiB.
     */
    void enforceEviction(const std::string& profileName,
                         const DeviceDumpStorage_t& storage);

    /**
     * @brief Check that the FDR partition has enough free space.
     * Uses GeneralConfig.PartitionThresoldCheckMB as threshold.
     */
    bool checkDiskSpace();

    /**
     * @brief Compute total bytes used by all files in dumpIndex_[profileName].
     */
    size_t computeUsedBytes(const std::string& profileName);

    /**
     * @brief Parse the trailing timestamp out of a dump filename.
     *
     * Filenames are {Section}.{ComponentID}.{FilePrefix}_{ts}.dat — the
     * timestamp is the final numeric segment before ".dat". Returns 0 if
     * parsing fails (the entry still lands in the queue; sort just
     * becomes a little noisy on that edge case).
     */
    static std::time_t extractTimestamp(const std::string& filepath);
};
