#pragma once
// ============================================================================
// Client-side session state (launch tokens, lobby state machine, bookkeeping).
//
// This module deliberately holds NO player data: hero, skin, talents, ratings,
// flag, league and the recommended stats are delivered by the server in
// NCore::PlayerInfo (Peered::ClientInfo -> gamesvc -> MapStartInfo). The
// server-side source of that record is Shared/WebSessionParse.h. It replaces
// the state that used to live in PF_GameLogic/WebLauncher.h.
// ============================================================================

#include <map>
#include <string>
#include <utility>
#include <vector>

// nstl:: types come from the per-module precompiled header (stdafx.h), the
// same way the removed PF_GameLogic/WebLauncher.h relied on.


// Login/lobby state of a web session, drives the lobby screens (Game.cpp,
// SelectGameModeScreen, SelectHeroScreen).
enum RegisterSessionRequest
{
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

// Result of the (optional) login handshake used by the dev/debug launch path.
enum LoginResponse
{
  LoginResponse_WEB_FAIL,

  LoginResponse_WEB_JOIN,
  LoginResponse_WEB_FAILED_CONNECTION,
};

struct WebLoginResponse
{
  WebLoginResponse() : retCode( LoginResponse_WEB_FAIL ) {}

  std::string      response;
  LoginResponse    retCode;
};

// Per-player bookkeeping of the running map (who is who, who left). Built by
// HeroSpawn from the player records the server delivered with the map.
struct PlayerSpawnInfo
{
  PlayerSpawnInfo() : teamId( -1 ), isLeaver( false ) {}

  nstl::wstring nickname;
  int           teamId;
  bool          isLeaver;
};


// --- launch / login state ---------------------------------------------------
extern std::string              g_devLogin;         // dev login name (debug launch)
extern std::string              g_sessionToken;     // 32 chars, from the launch protocol
extern std::string              g_playerToken;      // 64 chars playerKey, from the launch protocol
extern std::string              g_sessionName;      // session display name (reconnect lookup)
extern WebLoginResponse         g_webLoginResponse;
extern RegisterSessionRequest   g_sessionStatus;

// Match metadata of the web session (the game to create/join in the lobby).
// It describes the game, not the players, so it is not part of PlayerInfo.
extern std::string              g_mapId;
extern int                      g_playersCount;

// true for a locally hosted (non-network) game.
extern bool                     g_localGameRun;

// Chat mute of the local player, cached from NCore::PlayerInfo::chatMuted when
// the map players are initialized (see AdventureScreen::InitPlayerNames).
extern bool                     g_playerPwcChatMute;

// --- running map state ------------------------------------------------------
extern std::map<int, PlayerSpawnInfo>       userIdToNicknameMap;
extern nstl::vector<std::pair<int, int> >   playersKills;
