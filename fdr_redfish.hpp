/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#pragma once

#include <iostream>
#include <nlohmann/json.hpp>
#include "fdr_http.hpp"

class RedfishClient
{
private:
    HttpClient *httpc;

    std::string prefix;
    // user and password are optional
    // if both fields are specified, attempt to login to get the token.
    std::string user;
    std::string password;
    std::string token;

    // TODO: add config and logging

    inline bool need_login()
    {
        return (!this->user.empty()) && (!this->password.empty());
    }
    void login();
    void logout();

    nlohmann::json query_json(const std::string &uri);

public:
    RedfishClient(const std::string &prefix, const std::string &user = std::string{}, const std::string &password = std::string{});
    ~RedfishClient();

    // query the entire json document
    std::string query(const std::string &uri);

    // query part of the json document with json_pointer
    // see also https://json.nlohmann.me/features/json_pointer/
    std::string query_string(const std::string &uri, const std::string &json_pointer);
    std::uint64_t query_uint64t(const std::string &uri, const std::string &json_pointer);
    std::int64_t query_int64t(const std::string &uri, const std::string &json_pointer);
};
