/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for fdr_redfish.cpp — RedfishClient + HttpException/HttpResponse
 *
 * Note: RedfishClient owns a real HttpClient internally and all query methods
 * make real HTTP requests. Without being able to inject a mock HttpClient
 * (production code not modified), tests exercise construction/destruction
 * paths and internal state inspection only.
 */

// clang-format off
#include "testCommon.hpp"
#include "fdr_redfish.hpp"
// clang-format on

// --- RedfishClient construction ---

TEST(FdrRedfish, ConstructWithoutCredentials)
{
    EXPECT_NO_THROW({ RedfishClient client("http://127.0.0.1:0"); });
}

TEST(FdrRedfish, ConstructWithoutCredentials_InternalState)
{
    RedfishClient client("http://127.0.0.1:0");

    EXPECT_EQ(client.prefix, "http://127.0.0.1:0");
    EXPECT_TRUE(client.user.empty());
    EXPECT_TRUE(client.password.empty());
    EXPECT_TRUE(client.token.empty());
    EXPECT_FALSE(client.need_login());
}

TEST(FdrRedfish, NeedLoginTrueWhenCredentialsProvided)
{
    try
    {
        RedfishClient client("http://127.0.0.1:0", "admin", "password");
        EXPECT_TRUE(client.need_login());
        EXPECT_EQ(client.user, "admin");
        EXPECT_EQ(client.password, "password");
    }
    catch (...)
    {
        GTEST_SKIP() << "Constructor calls login() which requires a live "
                        "HTTP server; skipping in offline environment";
    }
}

TEST(FdrRedfish, HttpClientPointerIsNotNull)
{
    RedfishClient client("http://127.0.0.1:0");
    EXPECT_NE(client.httpc, nullptr);
}

// --- HttpException ---

TEST(FdrRedfish, HttpExceptionFields)
{
    HttpException ex(404, "Not Found");
    EXPECT_EQ(ex.code, 404);
    EXPECT_EQ(ex.message, "Not Found");
    std::string what = ex.what();
    EXPECT_THAT(what, HasSubstr("404"));
    EXPECT_THAT(what, HasSubstr("Not Found"));
}

TEST(FdrRedfish, HttpExceptionWhat)
{
    HttpException ex(500, "Internal Server Error");
    std::string what = ex.what();
    EXPECT_THAT(what, HasSubstr("HttpException"));
    EXPECT_THAT(what, HasSubstr("500"));
}

TEST(FdrRedfish, HttpExceptionZeroCode)
{
    HttpException ex(0, "");
    EXPECT_EQ(ex.code, 0);
    EXPECT_THAT(std::string(ex.what()), HasSubstr("0"));
}

// --- HttpResponse ---

TEST(FdrRedfish, HttpResponseDefaultValues)
{
    HttpResponse rsp{};
    EXPECT_EQ(rsp.status_code, 0);
    EXPECT_TRUE(rsp.body.empty());
}
