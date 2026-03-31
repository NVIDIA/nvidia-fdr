/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for fdr_redfish.cpp with stubbed HttpClient.
 * Links fdr_redfish.cpp + test_http_stubs.cpp (NOT fdr_http.cpp), so
 * every HTTP call returns a configurable response — no live server needed.
 *
 * Covers:
 *   - Constructor (no-cred, with-cred)
 *   - login() success and error paths
 *   - logout() normal and exception paths
 *   - query() 200, 401 re-login, non-200 throw, no-login+no-token
 *   - query_json() valid, empty, malformed
 *   - query_string() string / object / array / slash / empty pointer
 *   - query_uint64t() unsigned / non-unsigned
 *   - query_int64t() integer / non-integer
 */

// clang-format off
#include "testCommon.hpp"
#include "fdr_redfish.hpp"
// clang-format on

#include <deque>

extern HttpResponse g_stubHttpResponse;
extern bool g_stubHttpThrow;
extern int g_stubHttpThrowCode;
extern std::string g_stubHttpThrowMsg;
extern int g_stubHttpCallCount;
extern std::string g_stubHttpLastUrl;
extern std::deque<HttpResponse> g_stubHttpResponseQueue;

class RedfishStubTest : public Test
{
  protected:
    void SetUp() override
    {
        g_stubHttpResponse = {200, ""};
        g_stubHttpThrow = false;
        g_stubHttpThrowCode = 0;
        g_stubHttpThrowMsg.clear();
        g_stubHttpCallCount = 0;
        g_stubHttpLastUrl.clear();
        g_stubHttpResponseQueue.clear();
    }
};

// --- Constructor ---

TEST_F(RedfishStubTest, ConstructWithoutCredentials)
{
    RedfishClient client("http://stub:0");
    EXPECT_EQ(client.prefix, "http://stub:0");
    EXPECT_TRUE(client.user.empty());
    EXPECT_TRUE(client.password.empty());
    EXPECT_TRUE(client.token.empty());
    EXPECT_FALSE(client.need_login());
    EXPECT_NE(client.httpc, nullptr);
}

TEST_F(RedfishStubTest, ConstructWithCredentialsCallsLogin)
{
    g_stubHttpResponse = {200, R"({"token":"abc123"})"};

    RedfishClient client("http://stub:0", "admin", "pass");
    EXPECT_TRUE(client.need_login());
    EXPECT_EQ(client.token, "abc123");
}

TEST_F(RedfishStubTest, ConstructWithCredentialsLoginFailThrows)
{
    g_stubHttpResponse = {401, "Unauthorized"};

    EXPECT_THROW({ RedfishClient client("http://stub:0", "admin", "pass"); },
                 HttpException);
}

TEST_F(RedfishStubTest, ConstructWithCredentialsInvalidTokenThrows)
{
    g_stubHttpResponse = {200, R"({"token": 12345})"};

    EXPECT_THROW({ RedfishClient client("http://stub:0", "admin", "pass"); },
                 std::runtime_error);
}

// --- login() ---

TEST_F(RedfishStubTest, LoginSetsToken)
{
    g_stubHttpResponse = {200, R"({"token":"tok_xyz"})"};

    RedfishClient client("http://stub:0", "u", "p");
    EXPECT_EQ(client.token, "tok_xyz");
}

TEST_F(RedfishStubTest, LoginNon200Throws)
{
    g_stubHttpResponse = {500, "Internal error"};

    EXPECT_THROW({ RedfishClient client("http://stub:0", "u", "p"); },
                 HttpException);
}

// --- logout() ---

TEST_F(RedfishStubTest, LogoutCalledOnDestructorWhenTokenSet)
{
    g_stubHttpResponse = {200, R"({"token":"tok_abc"})"};

    int callsBefore;
    {
        RedfishClient client("http://stub:0", "u", "p");
        EXPECT_EQ(client.token, "tok_abc");
        g_stubHttpResponse = {200, ""};
        callsBefore = g_stubHttpCallCount;
    }

    EXPECT_GT(g_stubHttpCallCount, callsBefore);
    EXPECT_THAT(g_stubHttpLastUrl, HasSubstr("/logout"));
}

TEST_F(RedfishStubTest, LogoutNon200ResponseHandled)
{
    g_stubHttpResponse = {200, R"({"token":"tok_abc"})"};

    {
        RedfishClient client("http://stub:0", "u", "p");
        EXPECT_EQ(client.token, "tok_abc");
        g_stubHttpResponse = {503, "Service Unavailable"};
    }
    EXPECT_THAT(g_stubHttpLastUrl, HasSubstr("/logout"));
}

// --- query() ---

TEST_F(RedfishStubTest, QueryReturnsBodyOn200)
{
    RedfishClient client("http://stub:0");
    g_stubHttpResponse = {200, R"({"data":"value"})"};

    auto result = client.query("/test");
    EXPECT_EQ(result, R"({"data":"value"})");
}

TEST_F(RedfishStubTest, QueryThrowsOnNon200NoLogin)
{
    RedfishClient client("http://stub:0");
    g_stubHttpResponse = {500, "error"};

    EXPECT_THROW(client.query("/test"), HttpException);
}

TEST_F(RedfishStubTest, QueryNoLoginEmptyTokenReturnsEmpty)
{
    g_stubHttpResponse = {200, R"({"token":"tok"})"};
    RedfishClient client("http://stub:0", "u", "p");
    client.token.clear();

    auto result = client.query("/test");
    EXPECT_TRUE(result.empty());
}

TEST_F(RedfishStubTest, Query401ReloginSuccess)
{
    g_stubHttpResponse = {200, R"({"token":"tok"})"};
    RedfishClient client("http://stub:0", "u", "p");

    int callsBefore = g_stubHttpCallCount;

    g_stubHttpResponseQueue.push_back({401, "Unauthorized"});
    g_stubHttpResponseQueue.push_back({200, R"({"token":"tok2"})"});
    g_stubHttpResponseQueue.push_back({200, R"({"result":"ok"})"});

    auto result = client.query("/test");
    EXPECT_EQ(result, R"({"result":"ok"})");
    EXPECT_GE(g_stubHttpCallCount - callsBefore, 3);
}

// --- query_json() ---

TEST_F(RedfishStubTest, QueryJsonValidJson)
{
    RedfishClient client("http://stub:0");
    g_stubHttpResponse = {200, R"({"key":"val","num":42})"};

    auto j = client.query_json("/test");
    EXPECT_EQ(j["key"], "val");
    EXPECT_EQ(j["num"], 42);
}

TEST_F(RedfishStubTest, QueryJsonEmptyResponse)
{
    RedfishClient client("http://stub:0");
    g_stubHttpResponse = {200, ""};

    auto j = client.query_json("/test");
    EXPECT_TRUE(j.is_null());
}

TEST_F(RedfishStubTest, QueryJsonMalformedReturnsEmptyString)
{
    RedfishClient client("http://stub:0");
    g_stubHttpResponse = {200, "not json {{{"};

    auto j = client.query_json("/test");
    EXPECT_EQ(j, "");
}

// --- query_string() ---

TEST_F(RedfishStubTest, QueryStringExtractsValue)
{
    RedfishClient client("http://stub:0");
    g_stubHttpResponse = {200, R"({"name":"gpu0","id":1})"};

    auto result = client.query_string("/test", "/name");
    EXPECT_EQ(result, "gpu0");
}

TEST_F(RedfishStubTest, QueryStringObjectDumpsJson)
{
    RedfishClient client("http://stub:0");
    g_stubHttpResponse = {200, R"({"obj":{"a":1,"b":2}})"};

    auto result = client.query_string("/test", "/obj");
    EXPECT_THAT(result, HasSubstr("\"a\""));
}

TEST_F(RedfishStubTest, QueryStringArrayDumpsJson)
{
    RedfishClient client("http://stub:0");
    g_stubHttpResponse = {200, R"({"arr":[1,2,3]})"};

    auto result = client.query_string("/test", "/arr");
    EXPECT_THAT(result, HasSubstr("[1,2,3]"));
}

TEST_F(RedfishStubTest, QueryStringNonStringReturnsEmpty)
{
    RedfishClient client("http://stub:0");
    g_stubHttpResponse = {200, R"({"num":42})"};

    auto result = client.query_string("/test", "/num");
    EXPECT_TRUE(result.empty());
}

TEST_F(RedfishStubTest, QueryStringEmptyPointerReturnsFullQuery)
{
    RedfishClient client("http://stub:0");
    g_stubHttpResponse = {200, R"({"data":"all"})"};

    auto result = client.query_string("/test", "");
    EXPECT_EQ(result, R"({"data":"all"})");
}

TEST_F(RedfishStubTest, QueryStringSlashPointerReturnsFullQuery)
{
    RedfishClient client("http://stub:0");
    g_stubHttpResponse = {200, R"({"x":1})"};

    auto result = client.query_string("/test", "/");
    EXPECT_EQ(result, R"({"x":1})");
}

// --- query_uint64t() ---

TEST_F(RedfishStubTest, QueryUint64tUnsigned)
{
    RedfishClient client("http://stub:0");
    g_stubHttpResponse = {200, R"({"val":12345})"};

    auto result = client.query_uint64t("/test", "/val");
    EXPECT_EQ(result, 12345u);
}

TEST_F(RedfishStubTest, QueryUint64tNonUnsignedReturnsZero)
{
    RedfishClient client("http://stub:0");
    g_stubHttpResponse = {200, R"({"val":"not_a_number"})"};

    auto result = client.query_uint64t("/test", "/val");
    EXPECT_EQ(result, 0u);
}

TEST_F(RedfishStubTest, QueryUint64tNegativeReturnsZero)
{
    RedfishClient client("http://stub:0");
    g_stubHttpResponse = {200, R"({"val":-5})"};

    auto result = client.query_uint64t("/test", "/val");
    EXPECT_EQ(result, 0u);
}

// --- query_int64t() ---

TEST_F(RedfishStubTest, QueryInt64tInteger)
{
    RedfishClient client("http://stub:0");
    g_stubHttpResponse = {200, R"({"val":-42})"};

    auto result = client.query_int64t("/test", "/val");
    EXPECT_EQ(result, -42);
}

TEST_F(RedfishStubTest, QueryInt64tPositiveInteger)
{
    RedfishClient client("http://stub:0");
    g_stubHttpResponse = {200, R"({"val":999})"};

    auto result = client.query_int64t("/test", "/val");
    EXPECT_EQ(result, 999);
}

TEST_F(RedfishStubTest, QueryInt64tNonIntegerReturnsZero)
{
    RedfishClient client("http://stub:0");
    g_stubHttpResponse = {200, R"({"val":"string"})"};

    auto result = client.query_int64t("/test", "/val");
    EXPECT_EQ(result, 0);
}

TEST_F(RedfishStubTest, QueryInt64tFloatReturnsZero)
{
    RedfishClient client("http://stub:0");
    g_stubHttpResponse = {200, R"({"val":3.14})"};

    auto result = client.query_int64t("/test", "/val");
    EXPECT_EQ(result, 0);
}
