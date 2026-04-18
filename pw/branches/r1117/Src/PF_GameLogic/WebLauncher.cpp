#include "stdafx.h"
#include "WebLauncher.h"
#include <iostream>
#include <map>

#pragma comment(lib, "wininet.lib")

static std::set<std::string> allResourcesIDs;

extern string g_sessionToken;
extern string g_playerToken;
extern string g_sessionName;
extern bool g_spectatorStatus;
extern int g_playerTeamId;
extern int g_playerHeroId;
extern int g_playerPartyId;

bool g_playerPwcChatMute = false;

string g_devLogin;
string g_sessionToken;
string g_playerToken;
string g_mapId;

string g_sessionName;
WebLauncherPostRequest::RegisterSessionRequest g_sessionStatus;
bool g_spectatorStatus = false;
WebLauncherPostRequest::WebLoginResponse g_webLoginResponse;
int g_playerHeroId;
int g_playerPartyId;
bool g_localGameRun = false;

extern int g_playerTeamId;
extern int g_fixedTeamCam;


static std::map<std::wstring, int> s_userNicknameToUserIdMap;

int g_playersCount;

std::map<nstl::wstring, WebLauncherPostRequest::WebUserData> g_usersData;
map<int, WebLauncherPostRequest::PlayerInfoByUserId> userIdToNicknameMap;

nstl::vector<std::pair<int, int>> playersKills;


std::string GetFormattedJson(Json::Value value);


WebLauncherPostRequest::WebLauncherPostRequest()
{
  Init(SERVER_IP_W_ARRAY[usedServer], L"/api", SYNCHRONIZER_PORT, 0);
}

void WebLauncherPostRequest::Init(const wchar_t* serverUrl, const wchar_t* objectName, int serverPort, DWORD flags)
{
  const std::wstring apiUrl = serverUrl;

  //const std::string apiEndpoint = "/api/launcher/";

  hInternet = InternetOpenW(L"HTTP Post Request", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
  if (hInternet == NULL) {
    std::cerr << "InternetOpen failed with error: " << GetLastError() << std::endl;
    return;
  }

  // Connect to the server
  hConnect = InternetConnectW(hInternet, apiUrl.c_str(), serverPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
  if (hConnect == NULL) {
    std::cerr << "InternetConnect failed with error: " << GetLastError() << std::endl;
    InternetCloseHandle(hInternet);
    return;
  }

  // Open the HTTP request
  hRequest = HttpOpenRequestW(hConnect, L"POST", objectName, NULL, NULL, NULL, flags, 0);
  if (hRequest == NULL) {
    std::cerr << "HttpOpenRequest failed with error: " << GetLastError() << std::endl;
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return;
  }
}

WebLauncherPostRequest::~WebLauncherPostRequest()
{
	// Close handles
	InternetCloseHandle(hRequest);
	InternetCloseHandle(hConnect);
	InternetCloseHandle(hInternet);
}




std::string WebLauncherPostRequest::SendPostRequest(const std::string& jsonData) {
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


std::string GetSkinByHeroPersistentId(const std::string& heroPersistentId, int skinId)
{
  const char* heroes [] = {
    "prince",
    "snowqueen",
    "faceless",
    "warlord",
    "thundergod",
    "invisible",
    "mowgly",
    "inventor",
    "artist",
    "highlander",
    "marine",
    "firefox",
    "healer",
    "night",
    "rockman",
    "assassin",
    "unicorn",
    "hunter",
    "ghostlord",
    "ratcatcher",
    "archeress",
    "werewolf",
    "frogenglut",
    "witchdoctor",
    "manawyrm",
    "bard",
    "naga",
    "mage",
    "fairy",
    "witcher",
    "alchemist",
    "demonolog",
    "vampire",
    "witch",
    "crusader_A",
    "crusader_B",
    "monster",
    "angel",
    "freeze",
    "gunslinger",
    "reaper",
    "fluffy",
    "rifleman",
    "magicgirl",
    "pinkgirl",
    "ironknight",
    "fallenangel",
    "bladedancer",
    "ent",
    "plaguedoctor",
    "katana",
    "plane",
    "zealot",
    "wraithking",
    "dryad",
    "stalker",
    "gunner",
    "chronicle",
    "brewer",
    "shadow",
    "wendigo",
    "trickster",
    "banshee",
    "shaman",
    "bomber"
  };

  int heroId = 0;
  for (int i = 0; i < _countof(heroes); ++i) {
    if (heroPersistentId == heroes[i]) {
      heroId = i + 1;
      break;
    }
  }

  std::map<int, std::vector<std::string>> skinMap;

  skinMap[1].push_back("prince_S0_A");
  skinMap[1].push_back("prince_S1_A");
  skinMap[1].push_back("prince_S2_A");
  skinMap[1].push_back("prince_S4");
  skinMap[1].push_back("prince_S5");
  skinMap[1].push_back("prince_S0_B");
  skinMap[1].push_back("prince_S1_B");
  skinMap[1].push_back("prince_S2_B");
  skinMap[1].push_back("prince_S3");
  skinMap[2].push_back("snowqueen_S0_A");
  skinMap[2].push_back("snowqueen_S1_A");
  skinMap[2].push_back("snowqueen_S2_A");
  skinMap[2].push_back("snowqueen_S4");
  skinMap[2].push_back("snowqueen_S5");
  skinMap[2].push_back("snowqueen_S0_B");
  skinMap[2].push_back("snowqueen_S1_B");
  skinMap[2].push_back("snowqueen_S2_B");
  skinMap[2].push_back("snowqueen_S6");
  skinMap[2].push_back("snowqueen_S7");
  skinMap[3].push_back("faceless_S0_A");
  skinMap[3].push_back("faceless_S1_A");
  skinMap[3].push_back("faceless_S2_A");
  skinMap[3].push_back("faceless_S0_B");
  skinMap[3].push_back("faceless_S1_B");
  skinMap[3].push_back("faceless_S2_B");
  skinMap[3].push_back("faceless_S3");
  skinMap[4].push_back("warlord_S0_A");
  skinMap[4].push_back("warlord_S3");
  skinMap[4].push_back("warlord_S0_B");
  skinMap[4].push_back("warlord_S1_B");
  skinMap[4].push_back("warlord_S2");
  skinMap[4].push_back("warlord_S1_A");
  skinMap[5].push_back("thundergod_S0_A");
  skinMap[5].push_back("thundergod_S0_B");
  skinMap[5].push_back("thundergod_S1_B");
  skinMap[5].push_back("thundergod_S2_B");
  skinMap[5].push_back("thundergod_S4");
  skinMap[5].push_back("thundergod_S5");
  skinMap[5].push_back("thundergod_S6");
  skinMap[5].push_back("thundergod_S1_A");
  skinMap[5].push_back("thundergod_S2_A");
  skinMap[6].push_back("invisible_S0_A");
  skinMap[6].push_back("invisible_S1_B");
  skinMap[6].push_back("invisible_S2");
  skinMap[6].push_back("invisible_S4");
  skinMap[6].push_back("invisible_S1_A");
  skinMap[6].push_back("invisible_S0_B");
  skinMap[7].push_back("mowgly_S0_A");
  skinMap[7].push_back("mowgly_S0_B");
  skinMap[7].push_back("mowgly_S1_B");
  skinMap[7].push_back("mowgly_S2_B");
  skinMap[7].push_back("mowgly_S2_A");
  skinMap[7].push_back("mowgly_S1_A");
  skinMap[8].push_back("inventor");
  skinMap[8].push_back("inventor_S3");
  skinMap[8].push_back("inventor_S2");
  skinMap[8].push_back("inventor_S1");
  skinMap[9].push_back("artist");
  skinMap[9].push_back("artist_S3");
  skinMap[9].push_back("artist_S1");
  skinMap[9].push_back("artist_S2");
  skinMap[9].push_back("artist_S4");
  skinMap[10].push_back("highlander_S0_A");
  skinMap[10].push_back("highlander_S3_B");
  skinMap[10].push_back("highlander_S4");
  skinMap[10].push_back("highlander_S1_A");
  skinMap[10].push_back("highlander_S2_B");
  skinMap[10].push_back("highlander_S3_A");
  skinMap[10].push_back("highlander_S0_B");
  skinMap[10].push_back("highlander_S1_B");
  skinMap[10].push_back("highlander_S2_A");
  skinMap[11].push_back("marine_S0_A");
  skinMap[11].push_back("marine_S1_B");
  skinMap[11].push_back("marine_S2_B");
  skinMap[11].push_back("marine_S3_B");
  skinMap[11].push_back("marine_S4");
  skinMap[11].push_back("marine_S5");
  skinMap[11].push_back("marine_S1_A");
  skinMap[11].push_back("marine_S2_A");
  skinMap[11].push_back("marine_S0_B");
  skinMap[11].push_back("marine_S3_A");
  skinMap[12].push_back("firefox_S0_A");
  skinMap[12].push_back("firefox_S1_A");
  skinMap[12].push_back("firefox_S2_A");
  skinMap[12].push_back("firefox_S3");
  skinMap[12].push_back("firefox_S4_A");
  skinMap[12].push_back("firefox_S0_B");
  skinMap[12].push_back("firefox_S1_B");
  skinMap[12].push_back("firefox_S2_B");
  skinMap[12].push_back("firefox_S4_B");
  skinMap[12].push_back("firefox_S5");
  skinMap[13].push_back("healer_S0_A");
  skinMap[13].push_back("healer_S3_A");
  skinMap[13].push_back("healer_S4_A");
  skinMap[13].push_back("healer_S0_B");
  skinMap[13].push_back("healer_S1_B");
  skinMap[13].push_back("healer_S3_B");
  skinMap[13].push_back("healer_S4_B");
  skinMap[13].push_back("healer_S1_A");
  skinMap[13].push_back("healer_S2");
  skinMap[14].push_back("night_S0_A");
  skinMap[14].push_back("night_S3_A");
  skinMap[14].push_back("night_S4");
  skinMap[14].push_back("night_S1_B");
  skinMap[14].push_back("night_S0_B");
  skinMap[14].push_back("night_S2_B");
  skinMap[14].push_back("night_S5");
  skinMap[14].push_back("night_S1_A");
  skinMap[14].push_back("night_S2_A");
  skinMap[14].push_back("night_S3_B");
  skinMap[15].push_back("rockman_S0_A");
  skinMap[15].push_back("rockman_S2_A");
  skinMap[15].push_back("rockman_S0_B");
  skinMap[15].push_back("rockman_S1_B");
  skinMap[15].push_back("rockman_S2_B");
  skinMap[15].push_back("rockman_S3");
  skinMap[15].push_back("rockman_S1_A");
  skinMap[16].push_back("assassin_S0_A");
  skinMap[16].push_back("assassin_S1_B");
  skinMap[16].push_back("assassin_S3");
  skinMap[16].push_back("assassin_S4");
  skinMap[16].push_back("assassin_S1_A");
  skinMap[16].push_back("assassin_S2");
  skinMap[16].push_back("assassin_S0_B");
  skinMap[16].push_back("assassin_S5");
  skinMap[16].push_back("assassin_S6");
  skinMap[17].push_back("unicorn_S0_A");
  skinMap[17].push_back("unicorn_S3");
  skinMap[17].push_back("unicorn_S0_B");
  skinMap[17].push_back("unicorn_S1_B");
  skinMap[17].push_back("unicorn_S1_A");
  skinMap[18].push_back("hunter_S0_A");
  skinMap[18].push_back("hunter_S1_B");
  skinMap[18].push_back("hunter_S2_B");
  skinMap[18].push_back("hunter_S1_A");
  skinMap[18].push_back("hunter_S2_A");
  skinMap[18].push_back("hunter_S3");
  skinMap[18].push_back("hunter_S0_B");
  skinMap[19].push_back("ghostlord_S0_A");
  skinMap[19].push_back("ghostlord_S2_A");
  skinMap[19].push_back("ghostlord_S0_B");
  skinMap[19].push_back("ghostlord_S1_B");
  skinMap[19].push_back("ghostlord_S2_B");
  skinMap[19].push_back("ghostlord_S3");
  skinMap[19].push_back("ghostlord_S1_A");
  skinMap[20].push_back("ratcatcher_S0_A");
  skinMap[20].push_back("ratcatcher_S1_A");
  skinMap[20].push_back("ratcatcher_S0_B");
  skinMap[20].push_back("ratcatcher_S1_B");
  skinMap[20].push_back("ratcatcher_S2");
  skinMap[21].push_back("archeress_S0_A");
  skinMap[21].push_back("archeress_S4");
  skinMap[21].push_back("archeress_S5");
  skinMap[21].push_back("archeress_S6");
  skinMap[21].push_back("archeress_S7");
  skinMap[21].push_back("archeress_S1_A");
  skinMap[21].push_back("archeress_S2_A");
  skinMap[21].push_back("archeress_S3");
  skinMap[21].push_back("archeress_S0_B");
  skinMap[21].push_back("archeress_S1_B");
  skinMap[21].push_back("archeress_S2_B");
  skinMap[22].push_back("werewolf_S0_A");
  skinMap[22].push_back("werewolf_S3");
  skinMap[22].push_back("werewolf_S4");
  skinMap[22].push_back("werewolf_S1_A");
  skinMap[22].push_back("werewolf_S0_B");
  skinMap[22].push_back("werewolf_S1_B");
  skinMap[22].push_back("werewolf_S2");
  skinMap[23].push_back("frogenglut_S0_A");
  skinMap[23].push_back("frogenglut_S1_A");
  skinMap[23].push_back("frogenglut_S2_A");
  skinMap[23].push_back("frogenglut_S0_B");
  skinMap[23].push_back("frogenglut_S1_B");
  skinMap[23].push_back("frogenglut_S2_B");
  skinMap[24].push_back("witchdoctor_S0_A");
  skinMap[24].push_back("witchdoctor_S2");
  skinMap[24].push_back("witchdoctor_S1_A");
  skinMap[24].push_back("witchdoctor_S0_B");
  skinMap[24].push_back("witchdoctor_S1_B");
  skinMap[25].push_back("manawyrm_S0_A");
  skinMap[25].push_back("manawyrm_S1_A");
  skinMap[25].push_back("manawyrm_S0_B");
  skinMap[25].push_back("manawyrm_S1_B");
  skinMap[25].push_back("manawyrm_S2");
  skinMap[25].push_back("manawyrm_S3");
  skinMap[25].push_back("manawyrm_S4");
  skinMap[26].push_back("bard_S0_A");
  skinMap[26].push_back("bard_S2");
  skinMap[26].push_back("bard_S0_B");
  skinMap[26].push_back("bard_S1_B");
  skinMap[26].push_back("bard_S1_A");
  skinMap[27].push_back("naga_S0_B");
  skinMap[27].push_back("naga_S3");
  skinMap[27].push_back("naga_S1_A");
  skinMap[27].push_back("naga_S1_B");
  skinMap[27].push_back("naga_S2");
  skinMap[27].push_back("naga_S4");
  skinMap[27].push_back("naga_S0_A");
  skinMap[28].push_back("mage_S0_A");
  skinMap[28].push_back("mage_S1_B");
  skinMap[28].push_back("mage_S2");
  skinMap[28].push_back("mage_S4");
  skinMap[28].push_back("mage_S1_A");
  skinMap[28].push_back("mage_S2");
  skinMap[28].push_back("mage_S3");
  skinMap[28].push_back("mage_S0_B");
  skinMap[29].push_back("fairy_S0_A");
  skinMap[29].push_back("fairy_S1_B");
  skinMap[29].push_back("fairy_S2");
  skinMap[29].push_back("fairy_S0_B");
  skinMap[29].push_back("fairy_S1_A");
  skinMap[30].push_back("witcher");
  skinMap[30].push_back("witcher_S1");
  skinMap[30].push_back("witcher_S2");
  skinMap[30].push_back("witcher_S3");
  skinMap[30].push_back("witcher");
  skinMap[31].push_back("alchemist");
  skinMap[31].push_back("alchemist_S1");
  skinMap[32].push_back("demonolog");
  skinMap[32].push_back("demonolog_S2");
  skinMap[32].push_back("demonolog_S1");
  skinMap[33].push_back("vampire_S0_A");
  skinMap[33].push_back("vampire_S1_A");
  skinMap[33].push_back("vampire_S0_B");
  skinMap[33].push_back("vampire_S1_B");
  skinMap[33].push_back("vampire_S3");
  skinMap[33].push_back("vampire_S4");
  skinMap[33].push_back("vampire_S2");
  skinMap[34].push_back("witch_S0_A");
  skinMap[34].push_back("witch_S0_B");
  skinMap[34].push_back("witch_S1_B");
  skinMap[34].push_back("witch_S2_B");
  skinMap[34].push_back("witch_S1_A");
  skinMap[34].push_back("witch_S2_A");
  skinMap[34].push_back("witch_S3");
  skinMap[35].push_back("crusader_A");
  skinMap[35].push_back("crusader_S3_A");
  skinMap[35].push_back("crusader_S1_A");
  skinMap[36].push_back("crusader_B");
  skinMap[36].push_back("crusader_S2_B");
  skinMap[37].push_back("monster_S0_A");
  skinMap[37].push_back("monster_S0_B");
  skinMap[37].push_back("monster_S1_B");
  skinMap[37].push_back("monster_S1_A");
  skinMap[37].push_back("monster_S2");
  skinMap[38].push_back("angel");
  skinMap[38].push_back("angel_S2");
  skinMap[38].push_back("angel");
  skinMap[38].push_back("angel_S1_A");
  skinMap[39].push_back("freeze");
  skinMap[39].push_back("freeze_S2");
  skinMap[39].push_back("freeze");
  skinMap[39].push_back("freeze_S1");
  skinMap[40].push_back("gunslinger");
  skinMap[40].push_back("gunslinger_S1");
  skinMap[40].push_back("gunslinger_S2");
  skinMap[40].push_back("gunslinger_S3");
  skinMap[40].push_back("gunslinger");
  skinMap[41].push_back("reaper");
  skinMap[41].push_back("reaper");
  skinMap[41].push_back("reaper_S1");
  skinMap[41].push_back("reaper_S2");
  skinMap[42].push_back("fluffy");
  skinMap[42].push_back("fluffy_S1");
  skinMap[43].push_back("rifleman");
  skinMap[43].push_back("rifleman_S1");
  skinMap[44].push_back("magicgirl");
  skinMap[44].push_back("magicgirl_S1");
  skinMap[45].push_back("pinkgirl");
  skinMap[45].push_back("pinkgirl_S1");
  skinMap[46].push_back("ironknight");
  skinMap[46].push_back("ironknight_S1");
  skinMap[46].push_back("ironknight_S2");
  skinMap[47].push_back("fallenangel");
  skinMap[47].push_back("fallenangel_S2");
  skinMap[47].push_back("fallenangel_S1");
  skinMap[48].push_back("bladedancer");
  skinMap[48].push_back("bladedancer_S1");
  skinMap[49].push_back("ent");
  skinMap[49].push_back("ent_S1");
  skinMap[50].push_back("plaguedoctor");
  skinMap[50].push_back("plaguedoctor_S1");
  skinMap[50].push_back("plaguedoctor_S2");
  skinMap[50].push_back("plaguedoctor_S3");
  skinMap[51].push_back("katana");
  skinMap[51].push_back("katana_S1");
  skinMap[52].push_back("plane");
  skinMap[52].push_back("plane_S1");
  skinMap[53].push_back("zealot");
  skinMap[53].push_back("zealot_S1");
  skinMap[54].push_back("wraithking");
  skinMap[54].push_back("wraithking_S1");
  skinMap[55].push_back("dryad");
  skinMap[55].push_back("dryad_S1");
  skinMap[56].push_back("stalker");
  skinMap[56].push_back("stalker_S1");
  skinMap[57].push_back("gunner");
  skinMap[57].push_back("gunner_S1");
  skinMap[58].push_back("chronicle");
  skinMap[58].push_back("chronicle_S1");
  skinMap[59].push_back("brewer");
  skinMap[59].push_back("brewer_S1");
  skinMap[60].push_back("shadow");
  skinMap[60].push_back("shadow_S1");
  skinMap[61].push_back("wendigo");
  skinMap[61].push_back("wendigo_S1");
  skinMap[62].push_back("trickster");
  skinMap[62].push_back("trickster_S1");
  skinMap[63].push_back("banshee");
  skinMap[63].push_back("banshee_S1");
  skinMap[64].push_back("shaman");
  skinMap[64].push_back("shaman_S1");
  skinMap[65].push_back("bomber");
  skinMap[65].push_back("Bomber_S1");


  std::vector<std::string>& skins = skinMap[heroId];
  if (skins.empty()) {
    return "";
  }
  return skins[skinId % skins.size()];

}


void AddResourcePersistanceID(const char* data)
{
	allResourcesIDs.insert(data);
}



WebLauncherPostRequest::WebLoginResponse WebLauncherPostRequest::GetSessionData(const char* token, const char* apiKey)
{
  WebLoginResponse res;
  res.response = "";
  res.retCode = LoginResponse_WEB_FAILED_CONNECTION;

  std::string sessionToken(token, 32);
  std::string playerKey(token + 32);

  g_sessionToken = sessionToken.c_str();
  g_playerToken = playerKey.c_str();

  Json::Value data;
  data["sessionToken"] = Json::Value (sessionToken);
  data["playerKey"] = Json::Value (playerKey);

  Json::Value result;
  result["data"] = data;
  result["method"] = Json::Value("connectToWebSession");

  std::string req = GetFormattedJson(result);

  std::string responseStream = SendPostRequest(req);

  OutputDebugStringA(responseStream.c_str());

  std::string curNumber = "";

  Json::Value parsedJson = ParseJson(responseStream.c_str());
  if (parsedJson.empty()) {
    res.response = responseStream.c_str();
    res.retCode = LoginResponse_WEB_FAILED_CONNECTION;
    return res; // Failed json
  }

  Json::Value error = parsedJson.get("error", "ERROR");
  if (!error.asString().empty()) {
    res.response = error.asString().c_str();
    res.retCode = LoginResponse_WEB_FAIL;
    return res;
  }

  Json::Value playerInfo = parsedJson.get("playerInfo", Json::Value());
  if (playerInfo.empty()) {
    res.response = "playerInfo section is empty";
    res.retCode = LoginResponse_WEB_FAIL;
    return res;
  }
  if (!CheckPlayerInfo(playerInfo)) {
    res.response = "playerInfo section is not valid";
    res.retCode = LoginResponse_WEB_FAIL;
    return res;
  }

  Json::Value usersData = parsedJson.get("usersData", Json::Value());
  if (usersData.empty() || !usersData.isArray()) {
    res.response = "usersData section is empty or not array";
    res.retCode = LoginResponse_WEB_FAIL;
    return res;
  }

  Json::Value method = parsedJson.get("method", Json::Value());
  if (method.empty()) {
    res.response = "method section is empty";
    res.retCode = LoginResponse_WEB_FAIL;
    return res;
  }

  Json::Value mapId = parsedJson.get("mapId", Json::Value());
  if (mapId.empty()) {
    g_mapId = "Maps/Multiplayer/MOBA/Trening/_.ADMPDSCR.xdb";
  } else {
    g_mapId = mapId.asString().c_str();
  }

  Json::Value nickname = playerInfo.get("nickname", Json::Value());

  res.response = Fix1251Encoding(nickname.asString()).c_str();

  Json::Value hero = playerInfo.get("hero", Json::Value());
  Json::Value team = playerInfo.get("team", Json::Value());
  Json::Value party = playerInfo.get("party", Json::Value());
  g_playerHeroId = hero.asInt();
  g_playerTeamId = team.asInt() - 1;
  g_playerPartyId = party.asInt();

  Json::Value muteChat = playerInfo.get("muteChat", Json::Value(false));
  if (muteChat.asBool()) {
    g_playerPwcChatMute = true;
  }

  if (method.asString() == "create" || method.asString() == "connect" || method.asString() == "reconnect") {
    res.retCode = LoginResponse_WEB_JOIN;
    g_sessionStatus = RegisterInSessionRequest_WebJoin;
  }

  // Get users data
  g_playersCount = 0;
  Json::Value curPlayer = usersData[g_playersCount];
  while (!curPlayer.empty()) {
    if (!CheckPlayerInfo(curPlayer)) {
      res.response = "player info is not valid";
      res.retCode = LoginResponse_WEB_FAIL;
      return res;
    }

    std::string curNickname = curPlayer.get("nickname", Json::Value()).asString();

    std::wstring wideCharString = Fix1251EncodingW(curNickname);

    Json::Value userId = curPlayer.get("id", Json::Value());
    s_userNicknameToUserIdMap[wideCharString] = userId.asInt();
    
    WebUserData resData;
    Json::Value rating = curPlayer.get("rating", Json::Value());
	Json::Value ratingAcc = curPlayer.get("ratingAcc", Json::Value());
    resData.currentRating = rating.get("current", Json::Value()).asFloat();
    resData.victoryRating = rating.get("victory", Json::Value()).asFloat();
    resData.lossRating = rating.get("loss", Json::Value()).asFloat();
	resData.currentRatingAcc = ratingAcc.get("current", Json::Value()).asFloat();
	resData.victoryRatingAcc = ratingAcc.get("victory", Json::Value()).asFloat();
	resData.lossRatingAcc = ratingAcc.get("loss", Json::Value()).asFloat();
    resData.heroSkinID = curPlayer.get("skin", Json::Value()).asInt();
    resData.userId = curPlayer.get("id", Json::Value()).asInt();
    
    resData.talents.resize(36);

    Json::Value dataTalents = curPlayer.get("build", Json::Value());
    for (int i = 0; i < 36; ++i) {
      if (dataTalents[i].empty() || dataTalents[i].asInt() == 0) {
        resData.talents.clear();
        break; // empty slot in build
      }
      resData.talents[i].webTalentId = dataTalents[i].asInt();
    }
    if (!resData.talents.empty()) {
      Json::Value dataActives = curPlayer.get("bar", Json::Value());
      for (int a = 0; a < 24; ++a) {
        if (!dataActives[a].empty()) {
          int activeRaw = dataActives[a].asInt();
          if (activeRaw != 0) {
            int activeRef = abs(activeRaw) - 1;
            bool isSmartCast = activeRaw < 0;

            resData.talents[activeRef].activeSlot = a;
            resData.talents[activeRef].isSmartCast = isSmartCast;
          }
        }
      }
    }

    ZeroMemory(resData.profileStats, sizeof(resData.profileStats));
    Json::Value profileStats = curPlayer.get("profileStats", Json::Value());
    for (int i = 0; i < 9; ++i) {
      resData.profileStats[i] = profileStats[i].asInt();
    }

    g_usersData[wideCharString.c_str()] = resData;
    PlayerMetaInfo playerMetaInfo;
    playerMetaInfo.leagueIdx = curPlayer.get("leagueIdx", Json::Value(0)).asInt();
    playerMetaInfo.flagId = curPlayer.get("flagId", Json::Value("")).asString().c_str();

    userIdToMetaMap[userId.asInt()] = playerMetaInfo;


    g_playersCount++;
    curPlayer = usersData[g_playersCount];
  }
  g_localGameRun = g_playersCount == 1;

  return res;
}
#pragma optimize("", off)
std::string WebLauncherPostRequest::CreateDebugSession()
{
  std::string res = "";

  char jsonBuff[4096];
  ZeroMemory(jsonBuff,4096);

  Json::Value players;
  
  Json::Value player;
  player["id"] = Json::Value (131);
  player["nickname"] = Json::Value ("Rekongstor");
  player["muteChat"] = Json::Value (false);
  player["hero"] = Json::Value (65);
  player["team"] = Json::Value (1);
  player["party"] = Json::Value (0);
  player["skin"] = Json::Value (2);

  Json::Value rating;
  rating["current"] = Json::Value (2001.01234567);
  rating["victory"] = Json::Value (2021.987654321);
  rating["loss"] = Json::Value (1995.456789123123456);

  Json::Value build;
  build.resize(36);
  int buildArray[] = {689,634,413,576,415,377,687,632,370,510,426,723,686,631,605,508,677,676,-266,420,607,577,429,675,-263,-264,606,506,431,-265,-261,-262,406,507,564,-29};
  for (int i = 0; i < 36; ++i) {
    build[i] = buildArray[i];
  }
  Json::Value bar;
  bar.resize(10);
  int barArray[] = {-31,-32,30,19,8,0,0,0,0,0};
  for (int i = 0; i < 10; ++i) {
    bar[i] = barArray[i];
  }

  player["rating"] = rating;
  player["build"] = build;
  player["bar"] = bar;

  Json::Value player2 = player;
  player2["id"] = Json::Value (123456789);
  player2["nickname"] = Json::Value ("RekongstorRekongstor");
  player2["team"] = Json::Value (2);
  player2["leagueIdx"] = Json::Value(7);
  player2["flagId"] = Json::Value("guide");
  
  players.resize(2);
  players[0] = player;
  players[1] = player2;

  Json::Value data;
  data["sessionToken"] = Json::Value (SESSION_TOKEN);
  data["players"] = players;
  data["mapId"] = Json::Value("Maps/Multiplayer/MOBA/Trening/_.ADMPDSCR.xdb");

  Json::Value result;
  result["body"] = data;
  result["method"] = Json::Value("registerSession");
  result["key"] = Json::Value(API_KEY);

  std::string req = GetFormattedJson(result);

  std::string responseStream = SendPostRequest(req);

  OutputDebugStringA(responseStream.c_str());

  std::string curNumber = "";

  Json::Value parsedJson = ParseJson(responseStream.c_str());
  if (parsedJson.empty()) {
    res = responseStream.c_str();
    return res; // Failed json
  }

  Json::Value error = parsedJson.get("error", "ERROR");
  if (!error.asString().empty()) {
    res = error.asString().c_str();
    return res;
  }

  return res;
}
