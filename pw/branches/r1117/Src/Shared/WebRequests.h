#pragma once

#ifdef _WIN32
#include <Windows.h>
#include <Wininet.h>
#else
// typedef unsigned long DWORD;
typedef void* HINTERNET;
#endif
#include <vector>
#include <map>
#include <string>
#include <set>
#include <json/json.h>
#include "../PW_Game/server_ip.h"

class WebPostRequest
{
#ifdef _WIN32
	HINTERNET hInternet;
	HINTERNET hConnect;
	HINTERNET hRequest;
#endif
public:
	WebPostRequest(const wchar_t* serverUrl, const wchar_t* objectName, int serverPort, unsigned long flags);
  void Init(const wchar_t* serverUrl, const wchar_t* objectName, int serverPort, unsigned long flags);
  ~WebPostRequest();
  std::string SendPostRequest(const std::string& jsonData);
};

extern std::string GetSessionData(const char* token, bool registerSession);

extern int usedServer;

std::string GetFormattedJson(Json::Value value);