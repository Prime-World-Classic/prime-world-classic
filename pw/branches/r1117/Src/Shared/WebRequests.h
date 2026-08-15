#pragma once
#if defined( NV_WIN_PLATFORM )
#include <Windows.h>
#include <Wininet.h>
#include <vector>
#include <map>
#include <string>
#include <set>
#include <json/json.h>
#include "../PW_Game/server_ip.h"

class WebPostRequest
{
	HINTERNET hInternet;
	HINTERNET hConnect;
	HINTERNET hRequest;
public:
	WebPostRequest(const wchar_t* serverUrl, const wchar_t* objectName, int serverPort, DWORD flags);
  void Init(const wchar_t* serverUrl, const wchar_t* objectName, int serverPort, DWORD flags);
  ~WebPostRequest();
  std::string WebPostRequest::SendPostRequest(const std::string& jsonData);
};

extern std::string GetSessionData(const char* token, bool registerSession);

extern int usedServer;

std::string GetFormattedJson(Json::Value value);

#elif defined( NV_LINUX_PLATFORM )
#include <string>
#include <vector>
#include <map>
#include <set>
#include <json/json.h>
#include <curl/curl.h>
#include "../PW_Game/server_ip.h"

extern int usedServer;

class WebPostRequest
{
public:
  WebPostRequest(const wchar_t* serverUrl, const wchar_t* objectName, int serverPort, unsigned /*flags*/)
  {
    m_url = std::string("http://") + narrow(serverUrl)
          + ":" + std::to_string(serverPort) + "/" + narrow(objectName);
  }
  void Init(const wchar_t*, const wchar_t*, int, unsigned) {}
  ~WebPostRequest() {}

  std::string SendPostRequest(const std::string& jsonData)
  {
    std::string response;
    response.reserve(4096);
    CURLcode res;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (!curl)
      return response;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, m_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)jsonData.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &WebPostRequest::WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);   // 5s connect timeout (WinInet default ~)
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);          // 30s overall
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
      // Log the failure for diagnostics
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
  }

private:
  static size_t WriteCallback(void *data, size_t size, size_t nmemb, void *userdata)
  {
    std::string *str = static_cast<std::string *>(userdata);
    str->append(static_cast<char *>(data), size * nmemb);
    return size * nmemb;
  }

  static std::string narrow(const wchar_t *s)
  {
    if (!s) return std::string();
    std::string r;
    for (const wchar_t *p = s; *p; ++p)
      r += static_cast<char>(*p & 0x7F);
    return r;
  }

  std::string m_url;
};

inline std::string GetSessionData(const char* token, bool registerSession)
{
  WebPostRequest request(SERVER_IP_W, L"/api", SYNCHRONIZER_PORT, 0);

  Json::Value data;
  data["sessionToken"] = Json::Value(std::string(token, 32));
  data["apiKey"] = Json::Value(API_KEY);
  data["create"] = Json::Value(registerSession);

  Json::Value result;
  result["data"] = data;
  result["method"] = Json::Value("createWebSession");

  Json::FastWriter writer;
  std::string res = writer.write(result);

  return request.SendPostRequest(res);
}

#endif