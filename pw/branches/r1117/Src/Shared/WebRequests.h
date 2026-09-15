#pragma once

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

#include "ServerIps.h"

std::string GetFormattedJson(Json::Value value);