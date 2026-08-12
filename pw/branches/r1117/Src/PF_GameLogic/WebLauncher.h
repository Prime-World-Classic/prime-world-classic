#pragma once
#include <string>
#include <cwchar>
#include <cstdlib>

// Platform-independent declarations for encoding helpers
std::string WideCharToMultiByteString(const wchar_t* wideCharString);
std::string Fix1251Encoding(std::string utf8String);

#if defined( NV_WIN_PLATFORM )
#include <vector>
#include <Windows.h>
#include <Wininet.h>
#include <map>
#include <set>
#include <json/json.h>
#include "../PW_Game/server_ip.h"


class WebLauncherPostRequest
{
	HINTERNET hInternet;
	HINTERNET hConnect;
	HINTERNET hRequest;

	std::map<std::string, int> characterMap;
public:
  WebLauncherPostRequest();

  void Init(const wchar_t* serverUrl, const wchar_t* objectName, int serverPort, DWORD flags);

  ~WebLauncherPostRequest();

  enum LoginResponse {
    LoginResponse_WEB_FAIL,

    LoginResponse_WEB_JOIN,
    LoginResponse_WEB_FAILED_CONNECTION,
  };

  struct WebLoginResponse {
    std::string response;
    LoginResponse retCode;
  };

  struct TalentWebData {
    int webTalentId;
    int activeSlot; // negative = smart cast
    bool isSmartCast;
  };

  struct WebUserData {	
    WebUserData(): heroSkinID(0), currentRating(1100), victoryRating(1100), lossRating(1100), currentRatingAcc(1100), victoryRatingAcc(1100), lossRatingAcc(1100), userId(0) {}
    std::vector<TalentWebData> talents;
    int profileStats[9];
	  int heroSkinID;
    int userId;

    float currentRating;
    float victoryRating;
    float lossRating;
	float currentRatingAcc;
	float victoryRatingAcc;
	float lossRatingAcc;

    int heroId;
    int teamId;
    int partyId;
  };


  struct PlayerInfoByUserId {
    nstl::wstring nickname;
    int teamId;
    bool isLeaver;
    int userId;
  };

  struct PlayerMetaInfo {
    int leagueIdx;
    nstl::string flagId;
  };

  enum RegisterSessionRequest {
    RegisterInSessionRequest_Create,
    RegisterInSessionRequest_Wait,
    RegisterInSessionRequest_Connect,

    RegisterInSessionRequest_Reconnect,

    RegisterInSessionRequest_Joined,
    RegisterInSessionRequest_HeroSelected,
    RegisterInSessionRequest_InReadyState,

    RegisterInSessionRequest_WebCreate,
    RegisterInSessionRequest_WebConnect,
    RegisterInSessionRequest_WebReconnect,

    RegisterInSessionRequest_WebJoined,
    RegisterInSessionRequest_WebHeroSelected,

    RegisterInSessionRequest_WebJoin,
    RegisterInSessionRequest_WebJoinRetry,

    RegisterInSessionRequest_Error,
  };

  WebLoginResponse GetSessionData(const char* token, const char* apiKey = "");
  std::string WebLauncherPostRequest::SendPostRequest(const std::string& jsonData);
  std::string CreateDebugSession();
};
typedef std::map<std::wstring, WebLauncherPostRequest::WebUserData> WebUsersDataMap;

extern std::string GetSkinByHeroPersistentId(const std::string& heroId, int someValue);

static std::string WideCharToMultiByteString(const wchar_t* wideCharString) {
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, wideCharString, -1, NULL, 0, NULL, NULL);
  std::string result(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, wideCharString, -1, &result[0], size_needed, NULL, NULL);
  return result;
}
static std::wstring Fix1251EncodingW(std::string utf8String) {
  int utf8Length = static_cast<int>(utf8String.length());
  int wideCharLength = MultiByteToWideChar(CP_UTF8, 0, utf8String.c_str(), utf8Length, NULL, 0);

  std::wstring wideCharString;
  wideCharString.resize(wideCharLength);
  MultiByteToWideChar(CP_UTF8, 0, utf8String.c_str(), utf8Length, &wideCharString[0], wideCharLength);
  return wideCharString;
}
static std::string Fix1251Encoding(std::string utf8String)
{
  std::wstring wideCharString = Fix1251EncodingW(utf8String);

  int win1251Length = WideCharToMultiByte(1251, 0, &wideCharString[0], -1, NULL, 0, NULL, NULL);
  std::string win1251String;
  win1251String.resize(win1251Length, ' ');
  WideCharToMultiByte(1251, 0, &wideCharString[0], -1, &win1251String[0], win1251Length, NULL, NULL);

  return win1251String;
}

static Json::Value ParseJson(const char* json) {
  Json::Reader jsonReader;
  Json::Value root;
  bool isOk = jsonReader.parse(json, root, false);
  return isOk ? root : Json::Value();
}

static bool CheckPlayerInfo(const Json::Value& playerInfo)
{
  Json::Value nickname = playerInfo.get("nickname", Json::Value());
  if (nickname.empty() || !nickname.isString()) {
    OutputDebugStringA("Invalid nickname");
    return false;
  }
  Json::Value userId = playerInfo.get("id", Json::Value());
  if (userId.empty() || !userId.isInt()) {
    OutputDebugStringA("Invalid userId");
    return false;
  }
  Json::Value hero = playerInfo.get("hero", Json::Value());
  if (hero.empty() || !hero.isInt()) {
    OutputDebugStringA("Invalid hero");
    return false;
  }
  Json::Value team = playerInfo.get("team", Json::Value());
  if (team.empty() || !team.isInt()) {
    OutputDebugStringA("Invalid team");
    return false;
  }
  Json::Value party = playerInfo.get("party", Json::Value());
  if (party.empty() || !party.isInt()) {
    OutputDebugStringA("Invalid party");
    return false;
  }
  Json::Value skin = playerInfo.get("skin", Json::Value());
  if (skin.empty() || !skin.isInt()) {
    OutputDebugStringA("Invalid skin");
    return false;
  }
  Json::Value rating = playerInfo.get("rating", Json::Value());
  if (rating.empty()) {
    OutputDebugStringA("Invalid rating");
    return false;
  }
  {
    Json::Value current = rating.get("current", Json::Value());
    if (current.empty() || !current.isNumeric()) {
      OutputDebugStringA("Invalid rating::current");
      return false;
    }
    Json::Value victory = rating.get("victory", Json::Value());
    if (victory.empty() || !victory.isNumeric()) {
      OutputDebugStringA("Invalid rating::victory");
      return false;
    }
    Json::Value loss = rating.get("loss", Json::Value());
    if (loss.empty() || !loss.isNumeric()) {
      OutputDebugStringA("Invalid rating::loss");
      return false;
    }
  }
  Json::Value build = playerInfo.get("build", Json::Value());
  if (!build.isArray()) {
    OutputDebugStringA("Invalid build");
    return false;
  }
  Json::Value bar = playerInfo.get("bar", Json::Value());
  if (!bar.isArray()) {
    OutputDebugStringA("Invalid bar");
    return false;
  }

  return true;
}

extern std::map<nstl::wstring, WebLauncherPostRequest::WebUserData> g_usersData;
extern map<int, WebLauncherPostRequest::PlayerInfoByUserId> userIdToNicknameMap;
extern map<int, WebLauncherPostRequest::PlayerMetaInfo> userIdToMetaMap;

std::string GetSkinByHeroPersistentId(const std::string& heroPersistentId, int skinId);
// WideCharToMultiByteString and Fix1251Encoding — declared at top of file
#endif // NV_WIN_PLATFORM

// Linux implementations of encoding helpers
#if defined( NV_LINUX_PLATFORM )
#include <iconv.h>
#include <cstring>
inline std::string WideCharToMultiByteString(const wchar_t* wideCharString) {
  // wchar_t -> UTF-8 using iconv
  std::wstring ws(wideCharString);
  size_t wcsLen = ws.size();
  std::string out(wcsLen * 4, 0);
  iconv_t cd = iconv_open("UTF-8", "WCHAR_T");
  if (cd == (iconv_t)-1) {
    // Fallback: manual conversion for BMP characters
    out.clear();
    for (wchar_t c : ws) {
      if (c < 0x80) { out += (char)c; }
      else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
      else { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
    }
    return out;
  }
  const char* inPtr = reinterpret_cast<const char*>(&ws[0]);
  size_t inLeft = wcsLen * sizeof(wchar_t);
  char* outBuf = &out[0];
  size_t outLeft = out.size();
  size_t res = iconv(cd, const_cast<char**>(&inPtr), &inLeft, &outBuf, &outLeft);
  iconv_close(cd);
  if (res == (size_t)-1) return std::string();
  out.resize(out.size() - outLeft);
  return out;
}
inline std::string Fix1251Encoding(std::string utf8String) {
  iconv_t cd = iconv_open("CP1251", "UTF-8");
  if (cd == (iconv_t)-1) return utf8String;
  char* in = const_cast<char*>(utf8String.c_str());
  size_t inLeft = utf8String.size();
  std::string out(utf8String.size() * 2, 0);
  char* outBuf = &out[0];
  size_t outLeft = out.size();
  size_t res = iconv(cd, &in, &inLeft, &outBuf, &outLeft);
  iconv_close(cd);
  if (res == (size_t)-1) return utf8String;
  out.resize(out.size() - outLeft);
  return out;
}
#endif
