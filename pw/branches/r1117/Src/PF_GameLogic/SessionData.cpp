#include "stdafx.h"
#include "SessionData.h"

// Definitions of the client-side session state described in SessionData.h.
// Player data is not here: it arrives from the server in NCore::PlayerInfo.

std::string             g_devLogin;
std::string             g_sessionToken;
std::string             g_playerToken;
std::string             g_sessionName;
WebLoginResponse        g_webLoginResponse;
RegisterSessionRequest  g_sessionStatus = RegisterInSessionRequest_Create;

std::string             g_mapId;
int                     g_playersCount = 0;

bool                    g_localGameRun = false;
bool                    g_playerPwcChatMute = false;

std::map<int, PlayerSpawnInfo>      userIdToNicknameMap;
nstl::vector<std::pair<int, int> >  playersKills;
