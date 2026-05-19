#include "stdafx.h"
#include "SelectGameModeScreen.h"

#include "Core/CoreFSM.h"
#include "SelectGameModeLogic.h"
#include "PF_GameLogic/GameMaps.h"
#include "NewLobbyClientPW.h"

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
  logic->LoadScreenLayout( "Lobby_SelectGameMode" );

  {
    NI_PROFILE_BLOCK( "Maps" )
    StrongMT<Game::IGameContextUiInterface> locked = gameCtx.Lock();
    if ( !locked ) {
      return false;
    }

    NWorld::IMapCollection * maps = locked->Maps();
    NDb::Ptr<NDb::MapList> pMapList = NDb::Get<NDb::MapList>( NDb::DBID( "\\Tech\\Default\\_.MAPLST.xdb" ) );

    if ( !IsValid( pMapList ) ) {
      return false;
    }

    maps->InitCustomList(pMapList);

    for ( int i = 0; i < pMapList->maps.size(); ++i )
      logic->AddMapEntry( i, maps->CustomDescId( i ), maps->CustomTitle( i ), maps->CustomDescription( i ) );
  }

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

void SelectGameModeScreen::Step( bool bAppActive )
{
  StrongMT<Game::IGameContextUiInterface> locked = gameCtx.Lock();
  if ( !locked || !logic )
    return;

  lobby::EOperationResult::Enum joinResult = locked->LastLobbyOperationResult();
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
    int heroId = std::min(std::max((size_t)(size_t)(g_playerHeroId - 1), size_t(0)), size_t(_countof(heroes) - 1));
    locked->ChangeCustomGameSettings(lobby::ETeam::Enum(g_playerTeamId), lobby::ETeam::Enum(g_playerTeamId), heroes[heroId]);
    g_sessionStatus = WebLauncherPostRequest::RegisterInSessionRequest_HeroSelected;
  }
  if ((locked->GetLobbyStatus() == lobby::EClientStatus::InCustomLobby || g_localGameRun) && g_sessionStatus == WebLauncherPostRequest::RegisterInSessionRequest_WebJoined) {
    int heroId = std::min(std::max((size_t)(size_t)(g_playerHeroId - 1), size_t(0)), size_t(_countof(heroes) - 1));
    locked->ChangeCustomGameSettings(lobby::ETeam::Enum(g_playerTeamId), lobby::ETeam::Enum(g_playerTeamId), heroes[heroId]);
    locked->SetDeveloperParty(g_playerPartyId);
    g_sessionStatus = WebLauncherPostRequest::RegisterInSessionRequest_WebHeroSelected;
  }

  lobby::TDevGamesList infos;
  //locked->PopGameList( infos );

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
  if ( joinResult != lobby::EOperationResult::InProgress )
    logic->UpdateJoinResult( joinResult );

  DefaultScreenBase::Step( bAppActive );
}

} //namespace NGameX
