#include "stdafx.h"
#include "SessionData.h"

// Definitions of the client-side session state described in SessionData.h.
// Player data is not here: it arrives from the server in NCore::PlayerInfo.

nstl::string             g_devLogin;
nstl::string             g_sessionToken;
nstl::string             g_playerToken;
nstl::string             g_sessionName;
RegisterSessionRequest  g_sessionStatus = RegisterInSessionRequest_Create;

nstl::string             g_mapId;
int                     g_playersCount = 0;

bool                    g_localGameRun = false;
bool                    g_playerPwcChatMute = false;

std::map<int, PlayerSpawnInfo>      userIdToNicknameMap;
nstl::vector<std::pair<int, int> >  playersKills;
