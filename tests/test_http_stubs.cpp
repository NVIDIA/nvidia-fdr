/*
 * Stub HttpClient for fdr_redfish_stub_test.
 * Returns configurable responses so RedfishClient methods can be
 * exercised without a live HTTP server.
 */

#include "fdr_http.hpp"

#include <deque>

HttpResponse g_stubHttpResponse{200, ""};
bool g_stubHttpThrow = false;
int g_stubHttpThrowCode = 0;
std::string g_stubHttpThrowMsg;
int g_stubHttpCallCount = 0;
std::string g_stubHttpLastUrl;
std::deque<HttpResponse> g_stubHttpResponseQueue;

HttpException::HttpException(long code, std::string message) :
    code(code), message(std::move(message))
{
    message_ = "HttpException: " + std::to_string(this->code) + " " +
               this->message;
}

const char* HttpException::what() const noexcept
{
    return message_.c_str();
}

HttpClient::HttpClient() {}
HttpClient::~HttpClient() {}

HttpResponse
    HttpClient::request(const HttpRequestType& /*req_type*/,
                        const std::string& url,
                        const std::map<std::string, std::string>* /*headers*/,
                        const std::string* /*payload*/)
{
    ++g_stubHttpCallCount;
    g_stubHttpLastUrl = url;

    if (g_stubHttpThrow)
    {
        throw HttpException(g_stubHttpThrowCode, g_stubHttpThrowMsg);
    }
    if (!g_stubHttpResponseQueue.empty())
    {
        auto rsp = g_stubHttpResponseQueue.front();
        g_stubHttpResponseQueue.pop_front();
        return rsp;
    }
    return g_stubHttpResponse;
}
