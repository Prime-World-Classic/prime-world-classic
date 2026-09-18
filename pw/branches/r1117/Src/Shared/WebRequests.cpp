
#include "WebRequests.h"

int usedServer = 0;

#pragma comment(lib, "wininet.lib")

static nstl::vector<nstl::string> g_dynamicServerIpsA;
static nstl::vector<nstl::wstring> g_dynamicServerIpsW;

static bool IsHexDigit(char c)
{
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int HexCharToInt(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return c - 'A' + 10;
}

bool ParseServerIpsFromHex(const char* hexBlock, nstl::vector<nstl::string>& outIps)
{
  outIps.clear();

  if (!hexBlock)
    return false;

  size_t len = strlen(hexBlock);
  if (len == 0 || len % 8 != 0)
    return false;

  for (size_t i = 0; i < len; i += 8) {
    unsigned char octet[4];
    for (int j = 0; j < 4; ++j) {
      if (!IsHexDigit(hexBlock[i + j * 2]) || !IsHexDigit(hexBlock[i + j * 2 + 1]))
        return false;
      octet[j] = (unsigned char)((HexCharToInt(hexBlock[i + j * 2]) << 4) | HexCharToInt(hexBlock[i + j * 2 + 1]));
    }
    char buf[16];
    sprintf(buf, "%u.%u.%u.%u", (unsigned)octet[0], (unsigned)octet[1], (unsigned)octet[2], (unsigned)octet[3]);
    outIps.push_back(nstl::string(buf));
  }

  return !outIps.empty();
}

void SetDynamicServerIps(const nstl::vector<nstl::string>& ips)
{
  g_dynamicServerIpsA = ips;
  g_dynamicServerIpsW.clear();
  g_dynamicServerIpsW.reserve(ips.size());
  for (int i = 0; i < ips.size(); ++i) {
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, ips[i].c_str(), (int)ips[i].size(), NULL, 0);
    if (wideLen <= 0)
      continue;
    wchar_t* wbuf = new wchar_t[wideLen + 1];
    MultiByteToWideChar(CP_UTF8, 0, ips[i].c_str(), (int)ips[i].size(), wbuf, wideLen);
    wbuf[wideLen] = 0;
    g_dynamicServerIpsW.push_back(nstl::wstring(wbuf, (size_t)wideLen));
    delete[] wbuf;
  }
}

int GetServerIpCount()
{
  if (g_dynamicServerIpsA.empty())
    return _countof(SERVER_IP_ARRAY);
  return (int)g_dynamicServerIpsA.size();
}

const char* GetServerIpA(int index)
{
  if (g_dynamicServerIpsA.empty())
    return SERVER_IP_ARRAY[index % _countof(SERVER_IP_ARRAY)];
  int count = (int)g_dynamicServerIpsA.size();
  index %= count;
  if (index < 0)
    index += count;
  return g_dynamicServerIpsA[index].c_str();
}

const wchar_t* GetServerIpW(int index)
{
  if (g_dynamicServerIpsW.empty())
    return SERVER_IP_W_ARRAY[index % _countof(SERVER_IP_W_ARRAY)];
  int count = (int)g_dynamicServerIpsW.size();
  index %= count;
  if (index < 0)
    index += count;
  return g_dynamicServerIpsW[index].c_str();
}


WebPostRequest::WebPostRequest(const wchar_t* serverUrl, const wchar_t* objectName, int serverPort, DWORD flags)
{
  Init(serverUrl, objectName, serverPort, flags);
}


void WebPostRequest::Init(const wchar_t* serverUrl, const wchar_t* objectName, int serverPort, DWORD flags)
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
  DWORD postDataLen = jsonData.length();
  DWORD headersDataLen = strlen(headers);

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
  DWORD bytesRead = 0;
  std::string responseStream;

  while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
    buffer[bytesRead] = '\0'; // Null-terminate the buffer
    responseStream += buffer;
  }

  OutputDebugStringA(responseStream.c_str());

  return responseStream;
}

std::string GetSessionData(const char* token, bool registerSession) {
  WebPostRequest request(GetServerIpW(0), L"/api", SYNCHRONIZER_PORT, 0);

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

std::string GetWebSessionData(const char* token, const char* playerKey) {
  WebPostRequest request(SERVER_IP_W, L"/api", SYNCHRONIZER_PORT, 0);

  Json::Value data;
  data["sessionToken"] = Json::Value (std::string(token, 32));
  data["playerKey"] = Json::Value (playerKey);

  Json::Value result;
  result["data"] = data;
  result["method"] = Json::Value("connectToWebSession");

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