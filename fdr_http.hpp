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

#include <curl/curl.h>

#include <iostream>
#include <map>

class HttpException : public std::exception
{
  private:
    std::string message_;

  public:
    long code;
    std::string message;
    HttpException(long code, std::string message);
    const char* what() const noexcept override;
};

enum class HttpRequestType
{
    GET,
    POST,
};

class HttpResponse
{
  public:
    int status_code;
    std::string body;
};

class HttpClient
{
  private:
    // TODO: config and logging
  public:
    HttpClient();
    ~HttpClient();

    HttpResponse
        request(const HttpRequestType& req_type, const std::string& url,
                const std::map<std::string, std::string>* headers = nullptr,
                const std::string* payload = nullptr);

    inline HttpResponse
        get(const std::string& url,
            const std::map<std::string, std::string>* headers = nullptr)
    {
        return this->request(HttpRequestType::GET, url, headers, nullptr);
    };

    inline HttpResponse
        post(const std::string& url,
             const std::map<std::string, std::string>* headers = nullptr,
             const std::string* payload = nullptr)
    {
        return this->request(HttpRequestType::POST, url, headers, payload);
    };
};
