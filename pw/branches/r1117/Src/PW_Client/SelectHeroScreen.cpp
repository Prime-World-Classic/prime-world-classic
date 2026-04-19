#include "stdafx.h"
#include "SelectHeroScreen.h"

#include "Core/CoreFSM.h"
#include "SelectHeroScreenLogic.h"
#include "Client/MainTimer.h"
#include "System/InlineProfiler.h"
#include "../PF_GameLogic/WebLauncher.h"
#if !defined(__linux__)
#include "../PW_Game/server_ip.h"
#endif

extern string g_devLogin;
extern WebLauncherPostRequest::RegisterSessionRequest g_sessionStatus;
extern string g_sessionToken;
extern int g_playersCount;
extern bool g_localGameRun;

namespace NGameX
{  

SelectHeroScreen::SelectHeroScreen( Game::IGameContextUiInterface * _ctx ) :
gameCtx( _ctx ),
canBeKicked(false),
lobbyTimeout(0.f)
{   
}



bool SelectHeroScreen::Init( UI::User * uiUser )
{ 
  NI_PROFILE_FUNCTION

  logic = new UI::SelectHeroScreenLogic;
  SetLogic( logic );
  logic->SetUser( uiUser );

  if ( logic != NULL )
  {  
    logic->SetOwner( this );
    logic->LoadScreenLayout( "LobbyScreen" );

    logic->DebugDisplayPlayers( debugPlayerStatus );
  }
  else
    return false;

  return true; 
}   



void SelectHeroScreen::CommonStep( bool bAppActive )
{
  DefaultScreenBase::CommonStep( bAppActive );

  float dt = NMainLoop::GetTimeDelta();

  const float timeToReady = 60.0f;

  // 4. SetReady with all players connected or 
  if (g_sessionStatus == WebLauncherPostRequest::RegisterInSessionRequest_HeroSelected || g_sessionStatus == WebLauncherPostRequest::RegisterInSessionRequest_WebHeroSelected) {
    if (!logic->IsPlayerReady() && g_localGameRun) {
      g_sessionStatus = WebLauncherPostRequest::RegisterInSessionRequest_InReadyState;
      logic->PlayerReady();
      if ( StrongMT<Game::IGameContextUiInterface> locked = GameCtx().Lock() ) {
        if (locked->GetLobbyStatus() == lobby::EClientStatus::InCustomLobby) {
          locked->SetReady(lobby::EGameMemberReadiness::ReadyForAnything);
        }
      }
    } else {
      lobbyTimeout += dt;
      if (!logic->IsPlayerReady() && (debugPlayerIds.size() == g_playersCount + 2 || lobbyTimeout > timeToReady)) {
        g_sessionStatus = WebLauncherPostRequest::RegisterInSessionRequest_InReadyState;
        logic->PlayerReady();
        if ( StrongMT<Game::IGameContextUiInterface> locked = GameCtx().Lock() ) {
          if (locked->GetLobbyStatus() == lobby::EClientStatus::InCustomLobby) {
            locked->SetReady(lobby::EGameMemberReadiness::ReadyForAnything);
          }
        }
      }
    }
  } else {
    lobbyTimeout = 0;
  }


  bool update = false;
  for( map<int, float>::iterator it = debugHiliteTimes.begin(), next = it; it != debugHiliteTimes.end(); it = next )
  {
    next = it;
    ++next;

    it->second -= dt;
    if( it->second < 0 )
    {
      update = true;
      debugHiliteTimes.erase( it );
    }
  }

  if ( update )
  {
    DebugFormatPlayerInfo();
    logic->DebugDisplayPlayers( debugPlayerStatus );
  }

  logic->UpdateTimer((int)(debugPlayerIds.size() - 2));
}



void SelectHeroScreen::UpdatePlayers( const vector<wstring> & lines, const vector<int> & linesIds, const set<int> & hiliteIds )
{
  const float HILITE_TIME = 3.0f;

  debugPlayerLines = lines;
  debugPlayerIds = linesIds;

  for( set<int>::const_iterator it = hiliteIds.begin(); it != hiliteIds.end(); ++it )
    debugHiliteTimes[ *it ] = HILITE_TIME;

  DebugFormatPlayerInfo();

  if ( IsValid ( logic ) )
    logic->DebugDisplayPlayers ( debugPlayerStatus );
}



void SelectHeroScreen::DebugFormatPlayerInfo()
{
  if ( debugPlayerLines.size() != debugPlayerIds.size() )
    return;

  debugPlayerStatus = L"<style:gray>";

  for( int i = 0; i < debugPlayerLines.size(); ++i )
  {
    if ( i )
      debugPlayerStatus += L"<br>";

    bool hilite = debugHiliteTimes.find( debugPlayerIds[i] ) != debugHiliteTimes.end();
    if ( hilite )
      debugPlayerStatus += L"<style:red>";

    debugPlayerStatus += debugPlayerLines[i];

    if ( hilite )
      debugPlayerStatus += L"</style>";
  }
}

}//end of namespace
