/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace ::testing;

/*
 * Open up private/protected members for direct inspection in unit tests.
 * This must appear BEFORE any production header includes.
 * (Same pattern used by nsmd — see nsmd/test/commonMock.hpp)
 */
#define private public
#define protected public
