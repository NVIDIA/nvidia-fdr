/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for fdr_http.cpp — HttpClient, HttpException, HttpResponse
 *
 * Covers:
 *   - HttpException construction, what(), edge cases
 *   - HttpResponse default initialization
 *   - HttpClient construction
 *   - HTTP GET/POST to live endpoint (may be skipped in offline CI)
 *   - Invalid URL error handling
 */

#include "fdr_http.hpp"
#include "testCommon.hpp"

#include <cstdlib>

// --- HttpException (offline, no network needed) ---

TEST(FdrHttp, HttpExceptionConstruction)
{
    HttpException ex(503, "Service Unavailable");
    EXPECT_EQ(ex.code, 503);
    EXPECT_EQ(ex.message, "Service Unavailable");
}

TEST(FdrHttp, HttpExceptionWhat)
{
    HttpException ex(401, "Unauthorized");
    std::string what = ex.what();
    EXPECT_THAT(what, HasSubstr("401"));
    EXPECT_THAT(what, HasSubstr("Unauthorized"));
    EXPECT_THAT(what, HasSubstr("HttpException"));
}

TEST(FdrHttp, HttpExceptionZeroCode)
{
    HttpException ex(0, "");
    EXPECT_EQ(ex.code, 0);
    EXPECT_THAT(std::string(ex.what()), HasSubstr("0"));
}

// --- HttpResponse ---

TEST(FdrHttp, HttpResponseDefaultInit)
{
    HttpResponse rsp{};
    EXPECT_EQ(rsp.status_code, 0);
    EXPECT_TRUE(rsp.body.empty());
}

// --- HttpClient construction ---

TEST(FdrHttp, HttpClientConstruction)
{
    EXPECT_NO_THROW({ HttpClient client; });
}

// --- Invalid URL handling ---

TEST(FdrHttp, InvalidURLThrows)
{
    HttpClient client;
    EXPECT_THROW(client.get("http://0.0.0.0:1/nonexistent"), HttpException);
}

TEST(FdrHttp, RequestWithNullHeadersAndPayload)
{
    HttpClient client;
    EXPECT_THROW(client.request(HttpRequestType::GET, "http://0.0.0.0:1/test",
                                nullptr, nullptr),
                 HttpException);
}

// --- Live network tests (skipped in offline/Docker environments) ---

TEST(FdrHttp, HTTPrequest)
{
    if (std::getenv("OFFLINE_CI"))
    {
        GTEST_SKIP() << "Skipping live network test in offline CI";
    }

    HttpClient client;

    auto response = client.request(HttpRequestType::GET,
                                   "http://www.google.com/", nullptr, nullptr);
    ASSERT_EQ(response.status_code, 200);

    std::map<std::string, std::string> headers = {
        {"Content-Type", "application/json"},
    };
    std::string payload = R"({"hello": "world"})";
    response = client.request(HttpRequestType::POST, "http://www.google.com/",
                              &headers, &payload);
    ASSERT_EQ(response.status_code, 405);
}

TEST(FdrHttp, HTTPGet)
{
    if (std::getenv("OFFLINE_CI"))
    {
        GTEST_SKIP() << "Skipping live network test in offline CI";
    }

    HttpClient client;
    auto response = client.get("http://www.google.com/");
    ASSERT_EQ(response.status_code, 200);
}

TEST(FdrHttp, HTTPPost)
{
    if (std::getenv("OFFLINE_CI"))
    {
        GTEST_SKIP() << "Skipping live network test in offline CI";
    }

    HttpClient client;

    std::map<std::string, std::string> headers = {
        {"Content-Type", "application/json"},
    };
    std::string payload = R"({"hello": "world"})";
    auto response = client.post("http://www.google.com/", &headers, &payload);
    ASSERT_EQ(response.status_code, 405);
}
