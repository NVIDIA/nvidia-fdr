/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include <spdlog/spdlog.h>
#include "fdr_http.hpp"

HttpException::HttpException(long code, std::string message) : code(code)
{
  this->message = message;
  this->message_ = "HttpException Code: " + std::to_string(this->code) + ", Message: " + message;
}

const char *HttpException::what() const noexcept
{
  return this->message_.c_str();
}


size_t http_write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
  ((std::string *)userp)->append((char *)contents, size * nmemb);
  return size * nmemb;
}

HttpClient::HttpClient(){
    // TODO config and logging
}

HttpClient::~HttpClient(){

}

HttpResponse HttpClient::request(const HttpRequestType &req_type, const std::string &url, const std::map<std::string, std::string> *headers, const std::string *payload)
{
  // The curl pointer is created per request, so that it can be safely called by multiple threads
  // Alternatively, it can be re-used per HttpClient instance, but that would need some constrains, either add a locking, or each thread should create their own HttpClient instance.
  CURL *curl;
  CURLcode res;
  std::string readBuffer;

  HttpResponse response;

  spdlog::debug ("HttpClient requesting {}", url);

  curl = curl_easy_init();
  if (curl == NULL)
  {
    throw std::runtime_error("Error initializing curl");
  }

  curl_easy_reset(curl);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

  // set timeout, use 1000 milliseconds as a default value
  // TODO: read timeout from config file
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000);

  // FIXME: disable https verification
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

  // Callback for writing the response
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);

  if ((req_type == HttpRequestType::POST) && payload)
  {
    // TODO: other request type like PUT/PATCH/DElETE.
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload->c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload->size());
  }

  struct curl_slist *hdrs = NULL;
  if (headers)
  {
    for (auto const &[k, v] : *headers)
    {
      std::string header = k + ": " + v;
      hdrs = curl_slist_append(hdrs, header.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
  }

  res = curl_easy_perform(curl);
  if (hdrs != NULL)
  {
    curl_slist_free_all(hdrs);
  }
  if (res != CURLE_OK)
  {
    curl_easy_cleanup(curl);
    throw HttpException((long)-res, curl_easy_strerror(res));
  }

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);

  curl_easy_cleanup(curl);

  return response;
}
