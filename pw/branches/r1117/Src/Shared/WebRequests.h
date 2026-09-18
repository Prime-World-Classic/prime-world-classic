#pragma once
// The platform is selected by NV_LINUX_PLATFORM (defined only in the Linux build);
// everything else is treated as Windows: WIN32 / NV_WIN_PLATFORM are not visible
// to every project in this solution.

#include <string>
#include <vector>
#include <map>
#include <set>
#include <json/json.h>
#include "../PW_Game/server_ip.h"

// -----------------------------------------------------------------------------
// Web-session registry (the backend). It replaced the standalone synchronizer:
// the game server is just an HTTP client of the backend now.
// Address and key come from the service configuration (web_session_http_*);
// the constants in server_ip.h are only fallbacks for a local dev setup.
// -----------------------------------------------------------------------------

struct WebSessionEndpoint
{
  std::string   host;   // IPv4/dns, ASCII
  int           port;
  std::string   apiKey;
};

inline WebSessionEndpoint & WebSessionEndpointInstance()
{
  static WebSessionEndpoint endpoint = { SERVER_IP, BACKEND_HTTP_PORT, API_KEY };
  return endpoint;
}

// Пустые/нулевые аргументы не перекрывают уже установленное значение.
inline void SetWebSessionEndpoint( const char * host, int port, const char * apiKey )
{
  WebSessionEndpoint & endpoint = WebSessionEndpointInstance();

  if ( host && *host )
    endpoint.host = host;

  if ( port > 0 )
    endpoint.port = port;

  if ( apiKey && *apiKey )
    endpoint.apiKey = apiKey;
}

inline const WebSessionEndpoint & GetWebSessionEndpoint()
{
  return WebSessionEndpointInstance();
}

// {"method":<method>, "data":{<data>, "apiKey":<ключ>}} — форма, которую ждёт
// бэкенд (httpApi.js, плоские методы игрового сервера).
inline std::string BuildSessionRequest( const char * method, const Json::Value & data )
{
  Json::Value payload = data;

  if ( !payload.isObject() )
    payload = Json::Value( Json::objectValue );

  if ( !payload.isMember( "apiKey" ) )
    payload["apiKey"] = Json::Value( GetWebSessionEndpoint().apiKey );

  Json::Value body;
  body["data"]   = payload;
  body["method"] = Json::Value( method );

  Json::FastWriter writer;
  return writer.write( body );
}

// Транспорт: POST тела на бэкенд. Пустая строка — бэкенд недоступен.
std::string WebSessionHttpPost( const std::string & host, int port, const std::string & body );

inline std::string WebSessionRequest( const char * method, const Json::Value & data )
{
  const WebSessionEndpoint & endpoint = GetWebSessionEndpoint();
  return WebSessionHttpPost( endpoint.host, endpoint.port, BuildSessionRequest( method, data ) );
}

// Идентификация игрока при логине (backend 'connectToWebSession').
// Ответ: {"error":"", "playerInfo":{...}, "usersData":[...], "mapId":"..."}
inline std::string GetWebSessionData( const char * token, const char * playerKey )
{
  Json::Value data;
  data["sessionToken"] = Json::Value( std::string( token, 32 ) );
  data["playerKey"]    = Json::Value( playerKey );

  return WebSessionRequest( "connectToWebSession", data );
}

// Состав сессии и карта для лобби (backend 'createWebSession'); create=true
// помечает игру созданной (защита от двойного создания).
// Ответ: {"error":"", "usersData":[...], "mapId":"..."}
inline std::string CreateWebSession( const char * token, bool create )
{
  Json::Value data;
  data["sessionToken"] = Json::Value( std::string( token, 32 ) );
  data["create"]       = Json::Value( create );

  return WebSessionRequest( "createWebSession", data );
}


// -----------------------------------------------------------------------------
// Платформенный слой HTTP-запросов.
// -----------------------------------------------------------------------------

#if !defined( NV_LINUX_PLATFORM )

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

extern int usedServer;

// Dynamic server address registry (hex IP block from the launch protocol).
#include "ServerIps.h"

std::string GetFormattedJson(Json::Value value);

#elif defined( NV_LINUX_PLATFORM )

#include <curl/curl.h>

extern int usedServer;

inline std::string HttpPostJson(const std::string& url, const std::string& jsonData);

// libcurl write-callback (struct + static member: inline, ODR-safe in a header).
struct HttpPostSink
{
  static size_t Write(void * data, size_t size, size_t nmemb, void * userdata)
  {
    static_cast<std::string *>(userdata)->append(static_cast<char *>(data), size * nmemb);
    return size * nmemb;
  }
};

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
    return HttpPostJson(m_url, jsonData);
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

// Общий POST-хелпер (используется и WebPostRequest, и реестром веб-сессий).
inline std::string HttpPostJson(const std::string& url, const std::string& jsonData)
{
  std::string response;
  response.reserve(4096);

  curl_global_init(CURL_GLOBAL_DEFAULT);
  CURL *curl = curl_easy_init();
  if (!curl)
    return response;

  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)jsonData.size());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &HttpPostSink::Write);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);   // 5s connect timeout (WinInet default ~)
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);         // 30s overall
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  curl_easy_perform(curl);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return response;
}

inline std::string WebSessionHttpPost( const std::string & host, int port, const std::string & body )
{
  return HttpPostJson( std::string( "http://" ) + host + ":" + std::to_string( port ) + "/", body );
}

#endif
