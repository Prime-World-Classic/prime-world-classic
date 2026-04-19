#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <json/json.h>
#include "../PW_Game/server_ip.h"

#if defined(PW_LINUX_DB_BOOTSTRAP)

class WebPostRequest
{
public:
  WebPostRequest(const wchar_t* serverUrl, const wchar_t* objectName, int serverPort, unsigned long flags) {}
  void Init(const wchar_t* serverUrl, const wchar_t* objectName, int serverPort, unsigned long flags) {}
  std::string SendPostRequest(const std::string& jsonData) { return std::string(); }
};

extern std::string GetSessionData(const char* token, bool registerSession);
extern int usedServer;

std::string GetFormattedJson(Json::Value value);

#else

#include <Windows.h>
#include <Wininet.h>

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

#endif
