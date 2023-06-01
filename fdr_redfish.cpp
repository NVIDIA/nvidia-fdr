/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include "fdr_redfish.hpp"
#include "fdr_http.hpp"

using json = nlohmann::json;

RedfishClient::RedfishClient(const std::string &prefix, const std::string &user, const std::string &password) : prefix(prefix), user(user), password(password)
{
    this->httpc = new (HttpClient);

    if (this->need_login())
    {
        this->login();
    }

    std::cerr << "Created redfish client" << std::endl;
}

RedfishClient::~RedfishClient()
{

    if (!this->token.empty())
    {
        this->logout();
    }

    delete this->httpc;

    std::cerr << "Destroyed redfish client" << std::endl;
}

void RedfishClient::login()
{

    std::map<std::string, std::string> headers = {
        {"Content-Type", "application/json"},
    };

    json content = {
        {"username", this->user},
        {"password", this->password},
    };
    std::string payload = content.dump();

    HttpResponse rsp = this->httpc->post(this->prefix + "/login", &headers, &payload);

    if (rsp.status_code != 200)
    {
        std::cerr << "RedfishClient::login() error, status code: " << rsp.status_code << " body: " << rsp.body << std::endl;
        throw HttpException(rsp.status_code, rsp.body);
    }

    json j;
    j = json::parse(rsp.body);

    if (j["token"].is_string())
    {
        this->token = j["token"].get<std::string>();
    }
    else
    {

        throw std::runtime_error("RedfishClient::login() got invalid token");
    }
}

void RedfishClient::logout()
{
    HttpResponse rsp;
    try
    {
        rsp = this->httpc->post(this->prefix + "/logout", nullptr, nullptr);
    }
    catch (const HttpException &e)
    {
        std::cerr << "RedfishClient::logout() Error Code: " << e.code << ", message: " << e.what() << std::endl;
    }

    // 200: successfully logout
    // 401: already logout or session timed out
    // other status code might indicate some problem
    if (rsp.status_code != 200 || rsp.status_code != 401)
    {
        std::cerr << "RedfishClient::logout() Error Code: " << rsp.status_code << ", body: " << rsp.body << std::endl;
    }
}

std::string RedfishClient::query(const std::string &uri)
{
    std::map<std::string, std::string> headers;
    if (this->need_login())
    {
        if (this->token.empty())
        {
            std::cerr << "Redfish client is not logged in" << std::endl;
            return "";
        }
        else
        {
            headers["X-Auth-Token"] = this->token;
        }
    }

    std::string url = this->prefix + uri;

    HttpResponse rsp;

    rsp = this->httpc->get(url, &headers);
    if (rsp.status_code == 200)
    {
        return rsp.body;
    }
    else if ((rsp.status_code == 401) && this->need_login())
    {
        std::cerr << "Session probably timed out, try login again" << std::endl;
        this->login();

        // update the token header, is it needed?
        headers = {
            {"X-Auth-Token", this->token},
        };

        // TODO: could get error again, exception?
        HttpResponse rsp = this->httpc->get(url, &headers);
        if (rsp.status_code != 200)
        {
            throw HttpException(rsp.status_code, rsp.body);
        }
        return rsp.body;
    }
    else
    {
        throw HttpException(rsp.status_code, rsp.body);
    }
}

json RedfishClient::query_json(const std::string &uri)
{
    json j; // null object by default
    std::string result = this->query(uri);
    if (result.empty())
    {
        return j;
    }

    try
    {
        j = json::parse(result);
    }
    catch (json::parse_error &e)
    {
        std::cerr << "RedfishClient::query_json(): error parsing json at byte " << e.byte << " from input: " << result << std::endl;
        std::cerr << "RedfishClient::query_json(): return default value std::string{}" << std::endl;

        return "";
    }

    return j;
}

std::string RedfishClient::query_string(const std::string &uri, const std::string &json_pointer)
{
    std::cerr << ">>> uri: " << uri << " JSON Pointer: " << json_pointer << std::endl;
    if ((json_pointer == "") || (json_pointer == "/"))
    {
        // return the entire json as a string
        return this->query(uri);
    }

    json j = this->query_json(uri);

    auto val = j.at(json::json_pointer(json_pointer));
    if (val.is_string())
    {
        return val.get<std::string>();
    }
    else if (val.is_object())
    {
        std::cerr << "Val at " << json_pointer << " is an object" << std::endl;
        return val.dump();
    }
    else if (val.is_array())
    {
        std::cerr << "Val at " << json_pointer << " is an array" << std::endl;
        return val.dump();
    }

    std::cerr << "Val(" << val << ") at " << json_pointer << " is not string, return default value std::string{}" << std::endl;
    return "";
}

uint64_t RedfishClient::query_uint64t(const std::string &uri, const std::string &json_pointer)
{

    json j = this->query_json(uri);

    auto val = j.at(json::json_pointer(json_pointer));
    // if number < 0 (minus mark in json), it's signed
    // if number >= 0, it could be singed or unsigned, no way to distinguish them
    if (val.is_number_unsigned())
    {
        return val.get<uint64_t>();
    }

    std::cerr << "Val(" << val << ") at " << json_pointer << " is not unsigned int, return default value uint64_t{0}" << std::endl;
    return uint64_t{0};
}

int64_t RedfishClient::query_int64t(const std::string &uri, const std::string &json_pointer)
{

    json j = this->query_json(uri);

    auto val = j.at(json::json_pointer(json_pointer));
    // if number < 0(minus mark in json), it's signed
    // if number >= 0, it could be singed or unsigned, no way to distinguish them
    if (val.is_number_integer())
    {
        return val.get<int64_t>();
    }

    std::cerr << "Val(" << val << ") at " << json_pointer << " is not a integer number, return default value int64_t{0} " << std::endl;
    return int64_t{0};
}
