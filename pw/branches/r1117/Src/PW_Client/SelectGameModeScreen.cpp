#include "stdafx.h"
#include "SelectGameModeScreen.h"

#include "Core/CoreFSM.h"
#include "SelectGameModeLogic.h"
#include "PF_GameLogic/GameMaps.h"

#include "System/InlineProfiler.h"
#include "../PF_GameLogic/WebLauncher.h"

extern string g_sessionName;
extern WebLauncherPostRequest::RegisterSessionRequest g_sessionStatus;
extern int g_playerTeamId;
extern int g_playerHeroId;
extern int g_playerPartyId;
extern int g_playersCount;
extern std::string g_protocolToken;
extern bool g_localGameRun;
extern string g_mapId;

static string s_reconnect_hero = "rockman";
static int s_reconnect_team = 1;
REGISTER_VAR( "custom_game_reconnect_hero", s_reconnect_hero, STORAGE_NONE );
REGISTER_VAR( "custom_game_reconnect_team", s_reconnect_team, STORAGE_NONE );

namespace NGameX
{

void SelectGameModeScreen::SyncLinuxBootstrapUiState()
{
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
  if ( !logic )
    return;

  StrongMT<Game::IGameContextUiInterface> locked = gameCtx.Lock();
  const lobby::EOperationResult::Enum joinResult =
    locked ? locked->LastLobbyOperationResult() : lobby::EOperationResult::InProgress;
  logic->UpdateJoinResult( joinResult );
#endif
}


SelectGameModeScreen::SelectGameModeScreen( Game::IGameContextUiInterface * _ctx ) :
gameCtx( _ctx )
{ 
}


bool SelectGameModeScreen::Init( UI::User * uiUser )
{ 
  NI_PROFILE_FUNCTION

  logic = new UI::SelectGameModeLogic;
  SetLogic( logic );
  logic->SetUser( uiUser );
  logic->SetScreen( this );

  if ( !logic->LoadScreenLayout( "Lobby_SelectGameMode" ) )
    return false;

  {
    NI_PROFILE_BLOCK( "Maps" )
    StrongMT<Game::IGameContextUiInterface> locked = gameCtx.Lock();
    if ( !locked )
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
      return true;
#else
      return false;
#endif

    NWorld::IMapCollection * maps = locked->Maps();
    NDb::Ptr<NDb::MapList> pMapList = NDb::Get<NDb::MapList>( NDb::DBID( "\\Tech\\Default\\_.MAPLST.xdb" ) );

    NI_VERIFY( IsValid( pMapList ), "\\Tech\\Default\\_.MAPLST does not exist", return false )

    maps->InitCustomList(pMapList);

    for ( int i = 0; i < pMapList->maps.size(); ++i )
      logic->AddMapEntry( i, maps->CustomDescId( i ), maps->CustomTitle( i ), maps->CustomDescription( i ) );

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
    locked->RefreshGamesList();

    lobby::TDevGamesList infos;
    locked->PopGameList( infos );
    if ( infos.empty() )
    {
      const int bootstrapMaxPlayers = g_playersCount > 0 ? g_playersCount : 10;
      const char* primaryMapId = g_mapId.empty() ? "Maps/Multiplayer/ARAM/_.ADMPDSCR.xdb" : g_mapId.c_str();
      infos.push_back( lobby::SDevGameInfo( 1001, L"Linux Player's game", primaryMapId, 1, bootstrapMaxPlayers ) );
      infos.push_back( lobby::SDevGameInfo( 1002, L"ARAM practice", "Maps/Multiplayer/ARAM/_.ADMPDSCR.xdb", 2, 10 ) );
      infos.push_back( lobby::SDevGameInfo( 1003, L"Classic battle", "Maps/Multiplayer/MOBA/_.ADMPDSCR.xdb", 6, 10 ) );
    }
    for ( lobby::TDevGamesList::iterator it = infos.begin(); it != infos.end(); ++it )
      logic->UpdateSessionInfo( *it );
#endif
  }
/*
  if ( StrongMT<Game::IGameContextUiInterface> cl = gameCtx.Lock() )
    cl->RefreshGamesList();
*/
  return true; 
} 

static const char* heroes [] = {
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

static size_t GetHeroCount()
{
  return sizeof(heroes) / sizeof(heroes[0]);
}

void SelectGameModeScreen::Step( bool bAppActive )
{
  StrongMT<Game::IGameContextUiInterface> locked = gameCtx.Lock();
  if ( !logic )
    return;

  lobby::EOperationResult::Enum joinResult = lobby::EOperationResult::InProgress;

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
  if ( !locked )
  {
    DefaultScreenBase::Step( bAppActive );
    logic->UpdateJoinResult( lobby::EOperationResult::InProgress );
    return;
  }
#else
  if ( !locked )
    return;
#endif

  joinResult = locked->LastLobbyOperationResult();
  if (g_sessionStatus == WebLauncherPostRequest::RegisterInSessionRequest_WebJoinRetry) {
    if (joinResult == lobby::EOperationResult::InternalError) {
      locked->JoinWebGame(g_protocolToken.c_str());
      joinResult = lobby::EOperationResult::InProgress;
    }
    if (joinResult == lobby::EOperationResult::Ok) {
      g_sessionStatus = WebLauncherPostRequest::RegisterInSessionRequest_Error;
    }
  }
  if (g_sessionStatus == WebLauncherPostRequest::RegisterInSessionRequest_WebJoin) {
    if (joinResult == lobby::EOperationResult::InternalError) {
      locked->JoinWebGame(g_protocolToken.c_str());
      joinResult = lobby::EOperationResult::InProgress;
    }
    if (joinResult == lobby::EOperationResult::Ok) {
      locked->JoinWebGame(g_protocolToken.c_str());
      g_sessionStatus = WebLauncherPostRequest::RegisterInSessionRequest_WebJoinRetry;
    }
  }

  // 3. Select hero
  if (locked->GetLobbyStatus() == lobby::EClientStatus::InCustomLobby && g_sessionStatus == WebLauncherPostRequest::RegisterInSessionRequest_Joined) {
    const size_t heroCount = GetHeroCount();
    const size_t heroIndex = std::min(std::max(g_playerHeroId - 1, 0), static_cast<int>(heroCount - 1));
    int heroId = static_cast<int>(heroIndex);
    locked->ChangeCustomGameSettings(lobby::ETeam::Enum(g_playerTeamId), lobby::ETeam::Enum(g_playerTeamId), heroes[heroId]);
    g_sessionStatus = WebLauncherPostRequest::RegisterInSessionRequest_HeroSelected;
  }
  if ((locked->GetLobbyStatus() == lobby::EClientStatus::InCustomLobby || g_localGameRun) && g_sessionStatus == WebLauncherPostRequest::RegisterInSessionRequest_WebJoined) {
    const size_t heroCount = GetHeroCount();
    const size_t heroIndex = std::min(std::max(g_playerHeroId - 1, 0), static_cast<int>(heroCount - 1));
    int heroId = static_cast<int>(heroIndex);
    locked->ChangeCustomGameSettings(lobby::ETeam::Enum(g_playerTeamId), lobby::ETeam::Enum(g_playerTeamId), heroes[heroId]);
    locked->SetDeveloperParty(g_playerPartyId);
    g_sessionStatus = WebLauncherPostRequest::RegisterInSessionRequest_WebHeroSelected;
  }

  lobby::TDevGamesList infos;
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
  locked->PopGameList( infos );
#endif

  // 1. Create game for others
  if (g_sessionStatus == WebLauncherPostRequest::RegisterInSessionRequest_Create) {
    locked->CreateGame("Maps/Multiplayer/MOBA/_.ADMPDSCR.xdb", 10);
    g_sessionStatus = WebLauncherPostRequest::RegisterInSessionRequest_Joined;
  }
  if (g_sessionStatus == WebLauncherPostRequest::RegisterInSessionRequest_WebCreate) {
    if (g_localGameRun) {
      locked->CreateGame(g_mapId.c_str(), 10);
      g_sessionStatus = WebLauncherPostRequest::RegisterInSessionRequest_WebJoined;
    } else {
    locked->CreateGame("Maps/Multiplayer/MOBA/_.ADMPDSCR.xdb", g_playersCount);
    g_sessionStatus = WebLauncherPostRequest::RegisterInSessionRequest_WebJoined;
    }
  }

  // xxx Reconnect xxx
  if (g_sessionStatus == WebLauncherPostRequest::RegisterInSessionRequest_Reconnect || g_sessionStatus == WebLauncherPostRequest::RegisterInSessionRequest_WebReconnect) {
    int requiredGameId = -1;

    wstring nameTofind = Fix1251EncodingW(g_sessionName.c_str()).c_str();
    nameTofind += L"'s game";

    for( lobby::TDevGamesList::iterator it = infos.begin(); it != infos.end(); ++it ) {
      OutputDebugStringW(it->name.c_str());
      if (nameTofind.compare(it->name.c_str() + 1) == 0) {
        requiredGameId = it->gameId;
      }
    }
    if (requiredGameId != -1) {
      locked->Reconnect(requiredGameId, s_reconnect_team, s_reconnect_hero );
      if (g_sessionStatus == WebLauncherPostRequest::RegisterInSessionRequest_Reconnect) {
        g_sessionStatus = WebLauncherPostRequest::RegisterInSessionRequest_Joined;
      } else {
        g_sessionStatus = WebLauncherPostRequest::RegisterInSessionRequest_WebJoined;
      }
    }
  }
  

  for( lobby::TDevGamesList::iterator it = infos.begin(); it != infos.end(); ++it )
    logic->UpdateSessionInfo( *it );

  // 2. Connect to existing lobby by... session name
  if (g_sessionStatus == WebLauncherPostRequest::RegisterInSessionRequest_Connect || g_sessionStatus == WebLauncherPostRequest::RegisterInSessionRequest_WebConnect) {
    int requiredGameId = -1;

    wstring nameTofind = Fix1251EncodingW(g_sessionName.c_str()).c_str();
    nameTofind += L"'s game";

    for( lobby::TDevGamesList::iterator it = infos.begin(); it != infos.end(); ++it ) {
      OutputDebugStringW(it->name.c_str());
      if (nameTofind.compare(it->name.c_str() + 1) == 0) {
        requiredGameId = it->gameId;
      }
    }
    if (requiredGameId != -1) {
      locked->JoinGame(requiredGameId);
      if (g_sessionStatus == WebLauncherPostRequest::RegisterInSessionRequest_Connect) {
        g_sessionStatus = WebLauncherPostRequest::RegisterInSessionRequest_Joined;
      } else {
        g_sessionStatus = WebLauncherPostRequest::RegisterInSessionRequest_WebJoined;
      }
    } 
  }


  joinResult = locked->LastLobbyOperationResult();
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
  DefaultScreenBase::Step( bAppActive );
  SyncLinuxBootstrapUiState();
#else
  if ( joinResult != lobby::EOperationResult::InProgress )
    logic->UpdateJoinResult( joinResult );

  DefaultScreenBase::Step( bAppActive );
#endif
}

} //namespace NGameX
