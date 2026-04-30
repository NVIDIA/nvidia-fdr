/*
 Copyright (c) 2026, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include "fdr_device_dump_store.hpp"

#include "fdr_common.hpp"
#include "fdr_log.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

DeviceDumpStore::DeviceDumpStore(Profile_t& profile) : profile_(profile) {}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool DeviceDumpStore::canAccept()
{
    // Partition-level pre-check only. Per-profile BudgetMB is enforced by
    // enforceEviction() after every successful store — not here — so a
    // flooding device never trips a spurious reject, it just displaces the
    // globally-oldest dump in its profile queue.
    return checkDiskSpace();
}

bool DeviceDumpStore::store(const std::string& profileName,
                            const fdrpb::fdr_device_dump& pbDump,
                            const DeviceDumpProfile_t& dumpProfile,
                            const Component_t& component, std::time_t timestamp)
{
    auto finalPath = buildStoragePath(dumpProfile, component, timestamp);
    auto tempPath = finalPath + ".tmp";

    try
    {
        // Ensure the boot-count directory exists. DeviceDumpHandler creates
        // it at collectDump() time, but defending here keeps store()
        // self-sufficient against external cleanup between collection and
        // persistence.
        fs::create_directories(fs::path(tempPath).parent_path());

        std::ofstream outFile(tempPath, std::ios::binary | std::ios::trunc);
        if (!outFile.is_open())
        {
            fdrlog::error(
                "DeviceDumpStore: cannot open temp file '{}' for write",
                tempPath);
            return false;
        }

        if (!pbDump.SerializeToOstream(&outFile))
        {
            fdrlog::error(
                "DeviceDumpStore: protobuf serialization failed for {}",
                profileName);
            return false;
        }
        outFile.close();

        // Stream may have failed mid-write (ENOSPC, EIO) without
        // SerializeToOstream returning false. Check explicitly and refuse
        // to rename a half-written .tmp into place.
        if (outFile.fail())
        {
            fdrlog::error(
                "DeviceDumpStore: output stream in fail state after write "
                "for {} at {}",
                profileName, tempPath);
            try
            {
                fs::remove(tempPath);
            }
            catch (...)
            {}
            return false;
        }

        fs::rename(tempPath, finalPath);
    }
    catch (const std::exception& e)
    {
        fdrlog::error("DeviceDumpStore: failed to write/rename dump for {}: {}",
                      profileName, e.what());
        try
        {
            fs::remove(tempPath);
        }
        catch (...)
        {}
        return false;
    }

    dumpIndex_[profileName].push_back(DumpEntry{finalPath, timestamp});

    fdrlog::info("DeviceDumpStore: stored dump for profile '{}' at {}",
                 profileName, finalPath);

    enforceEviction(profileName, dumpProfile.Storage);

    return true;
}

void DeviceDumpStore::rebuildIndex()
{
    std::string basePath = profile_.GeneralConfig.LogsBasePath;

    for (const auto& dumpProfile : profile_.GeneralConfig.DeviceDumpProfiles)
    {
        for (const auto& section : profile_.Sections)
        {
            if (section.ID != dumpProfile.Section)
            {
                continue;
            }

            for (const auto& component : section.Components)
            {
                // Match files for this (section, component) produced by
                // this profile — filename prefix:
                // {Section}.{ComponentID}.{FilePrefix}_ All matches land in the
                // single per-profile queue regardless of which device/component
                // produced them.
                std::string filePrefix = dumpProfile.Section + "." +
                                         component.ID + "." +
                                         dumpProfile.Storage.FilePrefix + "_";

                try
                {
                    for (const auto& bootDir : fs::directory_iterator(basePath))
                    {
                        if (!bootDir.is_directory())
                        {
                            continue;
                        }

                        for (const auto& entry :
                             fs::directory_iterator(bootDir.path()))
                        {
                            if (!entry.is_regular_file())
                            {
                                continue;
                            }
                            auto fname = entry.path().filename().string();
                            if (fname.starts_with(filePrefix) &&
                                fname.ends_with(".dat"))
                            {
                                auto path = entry.path().string();
                                dumpIndex_[dumpProfile.ProfileName].push_back(
                                    DumpEntry{path, extractTimestamp(path)});
                            }
                        }
                    }
                }
                catch (const std::exception& e)
                {
                    fdrlog::debug("DeviceDumpStore: scan skipped for {}/{}: {}",
                                  dumpProfile.ProfileName, component.ID,
                                  e.what());
                }
            }
        }

        // Sort the profile's combined queue by timestamp so pop_front always
        // removes the globally-oldest dump within the profile.
        auto it = dumpIndex_.find(dumpProfile.ProfileName);
        if (it != dumpIndex_.end())
        {
            std::sort(it->second.begin(), it->second.end(),
                      [](const DumpEntry& a, const DumpEntry& b) {
                return a.timestamp < b.timestamp;
            });
        }
    }

    fdrlog::info("DeviceDumpStore: rebuilt index with {} profile queue(s)",
                 dumpIndex_.size());
}

std::string
    DeviceDumpStore::buildStoragePath(const DeviceDumpProfile_t& dumpProfile,
                                      const Component_t& component,
                                      std::time_t timestamp)
{
    // Flat layout: no subdirectories under the boot-count directory.
    // Filename follows FDR's existing telemetry naming convention (see
    // CreateSamplesWriter in fdr.cpp): <Section>.<ComponentID>.<type>_<ts>.dat
    // This matches telemetry files like "GPU.HGX_GPU_0.Event_<ts>.dat" so
    // Cleaner, Redfish download globs, and operator tooling treat dump
    // files consistently with existing FDR data.
    std::string basePath = profile_.GeneralConfig.LogsBasePath;
    std::string bootCountDir = GetDirectoryName();
    std::string section = dumpProfile.Section;
    std::string componentId = component.ID;
    std::string dumpName = dumpProfile.Storage.FilePrefix;

    return basePath + "/" + bootCountDir + "/" + section + "." + componentId +
           "." + dumpName + "_" + std::to_string(timestamp) + ".dat";
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void DeviceDumpStore::enforceEviction(const std::string& profileName,
                                      const DeviceDumpStorage_t& storage)
{
    auto it = dumpIndex_.find(profileName);
    if (it == dumpIndex_.end())
    {
        return;
    }
    auto& queue = it->second;

    const size_t budgetBytes = storage.BudgetMB * 1024UL * 1024UL;
    const size_t initialBytes = computeUsedBytes(profileName);

    // Log once when eviction actually fires (not on every store call). The
    // per-file "evicting …" lines below give the detailed trace.
    if (initialBytes > budgetBytes)
    {
        fdrlog::info("DeviceDumpStore: eviction fired for profile '{}' — "
                     "{} bytes used exceeds budget {} MB; evicting oldest",
                     profileName, initialBytes, storage.BudgetMB);
    }

    while (!queue.empty() && computeUsedBytes(profileName) > budgetBytes)
    {
        const auto& oldest = queue.front();
        fdrlog::info(
            "DeviceDumpStore: evicting oldest dump in profile '{}': {}",
            profileName, oldest.filepath);
        try
        {
            fs::remove(oldest.filepath);
        }
        catch (...)
        {}
        queue.pop_front();
    }
}

bool DeviceDumpStore::checkDiskSpace()
{
    auto thresholdMB = profile_.GeneralConfig.PartitionThresoldCheckMB;
    const std::string& basePath = profile_.GeneralConfig.LogsBasePath;

    try
    {
        auto spaceInfo = fs::space(basePath);
        auto availableMB = spaceInfo.available / (1024UL * 1024UL);

        if (availableMB <= thresholdMB)
        {
            fdrlog::warn("DeviceDumpStore: disk full ({}MB <= {}MB threshold), "
                         "skipping dump collection",
                         availableMB, thresholdMB);
            return false;
        }

        // Debug-level trace of partition headroom. Silenced at default log
        // level so it doesn't flood on per-collection canAccept() calls.
        fdrlog::debug(
            "DeviceDumpStore: disk ok — {} MB available, {} MB threshold",
            availableMB, thresholdMB);
    }
    catch (const std::exception& e)
    {
        // Fail-closed: if the kernel can't answer about partition state we
        // don't know whether a write would fit. Refuse to store rather than
        // risk filling eMMC under an unknown mount condition. FDR is
        // best-effort (FC-4) so dropping the dump is acceptable; silently
        // filling the partition would violate FC-8.
        fdrlog::error(
            "DeviceDumpStore: disk space check threw, fail-closed: {}",
            e.what());
        return false;
    }

    return true;
}

size_t DeviceDumpStore::computeUsedBytes(const std::string& profileName)
{
    auto it = dumpIndex_.find(profileName);
    if (it == dumpIndex_.end())
    {
        return 0;
    }

    size_t total = 0;
    for (const auto& entry : it->second)
    {
        try
        {
            if (fs::exists(entry.filepath))
            {
                total += fs::file_size(entry.filepath);
            }
        }
        catch (...)
        {}
    }
    return total;
}

std::time_t DeviceDumpStore::extractTimestamp(const std::string& filepath)
{
    // Filename form: {Section}.{ComponentID}.{FilePrefix}_{ts}.dat
    // Trailing numeric segment is the timestamp. Return 0 on parse failure
    // (entry still indexes; sort just becomes noisy on that edge case).
    auto fname = fs::path(filepath).filename().string();

    auto dotPos = fname.rfind(".dat");
    if (dotPos == std::string::npos)
    {
        return 0;
    }

    auto underscorePos = fname.rfind('_', dotPos);
    if (underscorePos == std::string::npos || underscorePos + 1 >= dotPos)
    {
        return 0;
    }

    try
    {
        return static_cast<std::time_t>(std::stoll(
            fname.substr(underscorePos + 1, dotPos - underscorePos - 1)));
    }
    catch (...)
    {
        return 0;
    }
}
