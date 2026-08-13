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

class WebPostRequest
{
public:
  WebPostRequest(const wchar_t*, const wchar_t*, int, unsigned) {}
  void Init(const wchar_t*, const wchar_t*, int, unsigned) {}
  ~WebPostRequest() {}
  std::string SendPostRequest(const std::string&) { return std::string(); }
};

inline std::string GetSessionData(const char*, bool) { return std::string(); }

#endif