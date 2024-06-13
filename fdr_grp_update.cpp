/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include "fdr_record.hpp"
#include "fdr_grp_update.hpp"
#include <telemetry_mrd_client.hpp>
#include "fdr_log.hpp"

void FdrGrpUpdate::RefreshAndStore(const std::vector<std::string>& keys,
                           const std::vector<Record *>& records) {

  if (keys.size() >= MIN_GROUP_KEY_SIZE) {

    // -- only supported Fetch Method is Shmem now -- // 

    if (keys[GROUP_KEY_METHOD_POS] == "Shmem") {
      auto shmemSampleData = nv::shmem::sensor_aggregation::getAllKeyValuePair(
          keys[GROUP_KEY_NAMESPACE_POS]);
      if (shmemSampleData.empty()) {
        fdrlog::error(
            "GroupRefreshAndStore: Got empty response from Shmem namespace:{}",
            keys[GROUP_KEY_NAMESPACE_POS]);
        return;
      }
      for (auto &rec : records) {
        if (shmemSampleData.find(rec->info.ShmemParams.Key) !=
            shmemSampleData.end()) {
          rec->RefreshValue(shmemSampleData[rec->info.ShmemParams.Key]);
          rec->Store();
        } else {
          fdrlog::error(
              "GroupRefreshAndStore: Key:{} not found in Shmem namespace:{}",
              rec->info.ShmemParams.Key, keys[GROUP_KEY_NAMESPACE_POS]);
        }
      }
    }
  }
}