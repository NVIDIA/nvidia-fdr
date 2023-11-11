#include <gtest/gtest.h>

#include "fdr_http.hpp"

TEST(FdrHttp, Sanity) {
    ASSERT_EQ(1, 1);
    ASSERT_NE(0, 1);
}

TEST(FdrHttp, HTTPrequest) {

    auto client = HttpClient();

    auto response = client.request(HttpRequestType::GET, "http://www.google.com/", nullptr, nullptr);
    ASSERT_EQ(response.status_code, 200);

    std::map<std::string, std::string> headers = {
        {"Content-Type", "application/json"},
    };
    std::string payload = R"({"hello": "world"})";
    response = client.request(HttpRequestType::POST, "http://www.google.com/", &headers, &payload);
    ASSERT_EQ(response.status_code, 405);
}

TEST(FdrHttp, HTTPGet) {
    auto client = HttpClient();

    auto response = client.get ("http://www.google.com/");

    ASSERT_EQ(response.status_code, 200);
}

TEST(FdrHttp, HTTPPost) {
    auto client = HttpClient();

    std::map<std::string, std::string> headers = {
        {"Content-Type", "application/json"},
    };
    std::string payload = R"({"hello": "world"})";
    auto response = client.post("http://www.google.com/", &headers, &payload);
    ASSERT_EQ(response.status_code, 405);
}