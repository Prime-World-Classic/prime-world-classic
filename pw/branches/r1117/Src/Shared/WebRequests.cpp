
#include "WebRequests.h"

int usedServer = 0;

#pragma comment(lib, "wininet.lib")


#ifdef _WIN32
WebPostRequest::WebPostRequest(const wchar_t* serverUrl, const wchar_t* objectName, int serverPort, unsigned long flags)
{
  Init(serverUrl, objectName, serverPort, flags);
}


void WebPostRequest::Init(const wchar_t* serverUrl, const wchar_t* objectName, int serverPort, unsigned long flags)
{
  const std::wstring apiUrl = serverUrl;

  //const std::string apiEndpoint = "/api/launcher/";

  hInternet = InternetOpenW(L"HTTP Post Request", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
  if (hInternet == NULL) {
    return;
  }

  // Connect to the server
  hConnect = InternetConnectW(hInternet, apiUrl.c_str(), serverPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
  if (hConnect == NULL) {
    InternetCloseHandle(hInternet);
    return;
  }

  // Open the HTTP request
  hRequest = HttpOpenRequestW(hConnect, L"POST", objectName, NULL, NULL, NULL, flags, 0);
  if (hRequest == NULL) {
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return;
  }
}

WebPostRequest::~WebPostRequest()
{
  // Close handles
  InternetCloseHandle(hRequest);
  InternetCloseHandle(hConnect);
  InternetCloseHandle(hInternet);
}

std::string WebPostRequest::SendPostRequest(const std::string& jsonData) {
  // Set headers and data for the POST request
  const char* headers = "Content-Type: application/json\r\n";
  const char* postData = jsonData.c_str();
  unsigned long postDataLen = jsonData.length();
  unsigned long headersDataLen = strlen(headers);

  // Send the HTTP request
  BOOL bRequestSent = HttpSendRequestA(hRequest, headers, headersDataLen, (LPVOID)postData, postDataLen);
  if (!bRequestSent) {
    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return "";
  }

  // Read the response
  char buffer[4096];
  unsigned long bytesRead = 0;
  std::string responseStream;

  while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
    buffer[bytesRead] = '\0'; // Null-terminate the buffer
    responseStream += buffer;
  }

  OutputDebugStringA(responseStream.c_str());

  return responseStream;
}
#else
WebPostRequest::WebPostRequest(const wchar_t*, const wchar_t*, int, unsigned long) {}
void WebPostRequest::Init(const wchar_t*, const wchar_t*, int, unsigned long) {}
WebPostRequest::~WebPostRequest() {}
std::string WebPostRequest::SendPostRequest(const std::string& jsonData) { return ""; }
#ifndef _WIN32
#define _snprintf_s(buf, size, fmt, ...) snprintf(buf, size, fmt, __VA_ARGS__)
#define _countof(a) (sizeof(a)/sizeof(*(a)))
#define SERVER_IP_W L"127.0.0.1"
#define SYNCHRONIZER_PORT 34980
#define API_KEY ""
#endif


#endif

std::string GetSessionData(const char* token, bool registerSession) {
  WebPostRequest request(SERVER_IP_W, L"/api", SYNCHRONIZER_PORT, 0);

  Json::Value data;
  data["sessionToken"] = Json::Value (std::string(token, 32));
  data["apiKey"] = Json::Value (API_KEY);
  data["create"] = Json::Value (registerSession);

  Json::Value result;
  result["data"] = data;
  result["method"] = Json::Value("createWebSession");

  Json::FastWriter writer;
  std::string res = writer.write(result);

  return request.SendPostRequest(res);
}

static void FormatJsonValue(Json::Value value, std::string& stream) {
  if (value.isObject()) {
    stream += "{";
    for (Json::Value::iterator it = value.begin(); it != value.end(); ++it) {
      if (it != value.begin()) {
        stream += ",";
      }
      stream += "\"";
      stream += it.memberName();
      stream += "\":";
      FormatJsonValue(it.operator *(), stream);
    }
    stream += "}";
    return;
  }
  if (value.isArray()) {
    stream += "[";
    int it = 0;
    Json::Value curValue = value[it];
    while (!curValue.empty()) {
      if (it != 0) {
        stream += ",";
      }
      FormatJsonValue(curValue, stream);
      ++it;
      curValue = value[it];
    }
    stream += "]";
    return;
  }
  if (value.isString()) {
    stream += "\"";
    stream += value.asString().c_str();
    stream += "\"";
    return;
  }
  if (value.isInt()) {
    char output[256];
    _snprintf_s(output, _countof(output), "%d", value.asInt());
    stream += output;
    return;
  }
  if (value.isDouble()) {
    char output[256];
    _snprintf_s(output, _countof(output), "%f", value.asDouble());
    stream += output;
    return;
  }
  if (value.isBool()) {
    stream += value.asBool() ? "true" : "false";
    return;
  }
  int i = 0;
}

std::string GetFormattedJson(Json::Value value) {
  std::string sStream;
  sStream.reserve(65536);
  FormatJsonValue(value, sStream);
  return sStream;
}