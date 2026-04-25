#include "stdafx.h"

#include "SelectGameModeLogic.h"

#include "SelectGameModeScreen.h"
#include "UI/EditBox.h"
#include "UI/ImageLabel.h"
#include "Server/LobbyPvx/CommonTypes.h"
#include "PF_GameLogic/GameMaps.h"
#include "Scripts/NameMap.h"

#include "System/InlineProfiler.h"


static string s_last_map = ""; //Used by Lua script
REGISTER_VAR( "last_map", s_last_map, STORAGE_USER );

static string s_reconnect_hero = "rockman";
static int s_reconnect_team = 1;
//REGISTER_VAR( "custom_game_reconnect_hero", s_reconnect_hero, STORAGE_NONE );
//REGISTER_VAR( "custom_game_reconnect_team", s_reconnect_team, STORAGE_NONE );


namespace UI
{

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
namespace
{
void PopulateLinuxBootstrapMapEntry(Window* games, int index, const char* id, const wchar_t* title, const wchar_t* description)
{
  Window* list = games->FindChild("List");
  if (!list)
  {
    return;
  }

  const string itemName = NStr::StrFmt("Item%d", index);
  Window* item = list->GetChild(itemName.c_str());
  if (!item)
  {
    item = list->CreateChild(itemName.c_str(), "item", 0, 0, -1, -1, NDb::UIELEMENTHALIGN_LEFT, NDb::UIELEMENTVALIGN_TOP);
  }
  if (!item)
  {
    return;
  }

  const char* mapId = id ? id : "";
  const string titleText = title ? NStr::ToMBCS(title) : "";
  const string descriptionText = description ? NStr::ToMBCS(description) : "";
  const bool selected = !s_last_map.empty() && s_last_map == mapId;

  if (ImageLabel* idWnd = GetChildChecked<ImageLabel>(item, "Id", true))
  {
    idWnd->SetCaptionTextA(mapId);
  }

  if (ImageLabel* titleWnd = GetChildChecked<ImageLabel>(item, "Title", true))
  {
    titleWnd->SetCaptionTextA(titleText.c_str());
    titleWnd->Show(!selected);
  }

  if (ImageLabel* activeTitleWnd = GetChildChecked<ImageLabel>(item, "TitleActive", true))
  {
    activeTitleWnd->SetCaptionTextA(titleText.c_str());
    activeTitleWnd->Show(selected);
  }

  if (ImageLabel* descriptionWnd = GetChildChecked<ImageLabel>(item, "Descr", true))
  {
    descriptionWnd->SetCaptionTextA(descriptionText.c_str());
  }

  if (Window* hiliteWnd = item->FindChild("Hilite"))
  {
    hiliteWnd->Show(false);
  }

  if (Window* activeBorderWnd = item->FindChild("ActiveBorder"))
  {
    activeBorderWnd->Show(selected);
  }
}

void PopulateLinuxBootstrapSessionEntry(
  Window* games,
  const lobby::SDevGameInfo& info,
  const char* mapTitle
)
{
  Window* list = games->FindChild("List");
  if (!list)
  {
    return;
  }

  char itemName[64] = {0};
  snprintf(itemName, sizeof(itemName), "Item%d", static_cast<int>(info.gameId));

  Window* item = list->GetChild(itemName);
  if (!item)
  {
    item = list->CreateChild(itemName, "item", 0, 0, -1, -1, NDb::UIELEMENTHALIGN_LEFT, NDb::UIELEMENTVALIGN_TOP);
  }
  if (!item)
  {
    return;
  }

  const string nameText = NStr::ToMBCS(info.name);
  char idText[64] = {0};
  snprintf(idText, sizeof(idText), "# %d", static_cast<int>(info.gameId));

  char playersText[64] = {0};
  snprintf(playersText, sizeof(playersText), "%d / %d", info.playersCount, info.maxPlayers);

  if (ImageLabel* nameWnd = GetChildChecked<ImageLabel>(item, "Name", true))
  {
    nameWnd->SetCaptionTextA(nameText.c_str());
  }

  if (ImageLabel* idWnd = GetChildChecked<ImageLabel>(item, "Id", true))
  {
    idWnd->SetCaptionTextA(idText);
  }

  if (ImageLabel* playersWnd = GetChildChecked<ImageLabel>(item, "Players", true))
  {
    playersWnd->SetCaptionTextA(playersText);
  }

  if (ImageLabel* mapWnd = GetChildChecked<ImageLabel>(item, "Map", true))
  {
    mapWnd->SetCaptionTextA(mapTitle ? mapTitle : info.mapId.c_str());
  }

  if (Window* startedWnd = item->FindChild("Started"))
  {
    startedWnd->Show(false);
  }

  if (Window* hiliteWnd = item->FindChild("Hilite"))
  {
    hiliteWnd->Show(false);
  }
}
}
#endif


#pragma warning(push)
#pragma warning(disable:4686)
BEGIN_LUA_TYPEINFO( SelectGameModeLogic, ClientScreenUILogicBase )
  LUA_METHOD( SetDeveloperSex )
  LUA_METHOD( CreateCustomGame )
  LUA_METHOD( JoinToGame )
  LUA_METHOD( RefreshList )
  LUA_METHOD( SetJoinMode )
END_LUA_TYPEINFO( SelectGameModeLogic )
#pragma warning(pop)



void SelectGameModeLogic::SetDeveloperSex( int _sex )
{
  NI_VERIFY( screen, "", return );

  if ( ( _sex == lobby::ESex::Male ) || ( _sex == lobby::ESex::Female ) )
    if ( StrongMT<Game::IGameContextUiInterface> cb = screen->GameCtx() )
      cb->SetDeveloperSex( (lobby::ESex::Enum)_sex );
}



void SelectGameModeLogic::CreateCustomGame( int playersNumber, const char * mapId )
{
  NI_VERIFY( screen, "", return );

  if ( screen->GameCtx() )
    screen->GameCtx()->CreateGame( mapId, playersNumber );
}



void SelectGameModeLogic::JoinToGame( int sessionId )
{
  NI_VERIFY( screen, "", return );

  UpdateJoinResult( lobby::EOperationResult::InProgress );

  if ( joinMode.find( "reconnect" ) != string::npos )
    screen->GameCtx()->Reconnect( sessionId, s_reconnect_team, s_reconnect_hero );
  else if ( joinMode.find( "spectate" ) != string::npos )
    screen->GameCtx()->Spectate( sessionId );
  else
    screen->GameCtx()->JoinGame( sessionId );
}



void SelectGameModeLogic::RefreshList()
{
  if ( screen )
    if ( screen->GameCtx() )
      screen->GameCtx()->RefreshGamesList();
}



void SelectGameModeLogic::SetJoinMode( const string & mode )
{
  joinMode = mode;
  NStr::ToLower( &joinMode );
}



void SelectGameModeLogic::AddMapEntry( int index, const char * id, const wchar_t * title, const wchar_t * description )
{
  NI_PROFILE_FUNCTION
  UI::Window * games = pBaseWindow->FindChild( "Maps" );
  if ( games )
  {
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
    PopulateLinuxBootstrapMapEntry(games, index, id, title, description);
#else
    games->CallHandler( "CppAddMap", index, id, NStr::ToMBCS( title ).c_str(), NStr::ToMBCS( description ).c_str() );
#endif
  }
}



void SelectGameModeLogic::UpdateSessionInfo( const lobby::SDevGameInfo & info )
{
  if (info.gameId > INT_MAX)
    return;

  UI::Window * games = pBaseWindow->FindChild( "Games" );
  if ( games )
  {
    string title = info.mapId;
    if ( screen && screen->GameCtx() )
    {
      NWorld::IMapCollection * maps = screen->GameCtx()->Maps();
      int mapIndex = maps->FindMapById( info.mapId.c_str() );
      if ( mapIndex >= 0 )
        title = NStr::ToMBCS( maps->MapTitle( mapIndex ) );
    }

    NScript::NamedValues params;
    params.SetValue( "currentPlayers", info.playersCount );
    params.SetValue( "maxPlayers", info.maxPlayers );
    params.SetValue( "name", NStr::ToMBCS( info.name ) );
    params.SetValue( "mapTitle", title );
    params.SetValue( "started", false );

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
    PopulateLinuxBootstrapSessionEntry(games, info, title.c_str());
#else
    games->CallHandler( "CppSessionInfo", (int)info.gameId, params );
 #endif
  }
}



void SelectGameModeLogic::UpdateJoinResult( lobby::EOperationResult::Enum result )
{
  const char * text = 0;
  switch ( result )
  {
    default:
    case lobby::EOperationResult::Ok:                 text = "Ok"; break;
    case lobby::EOperationResult::InProgress:         text = "Please, wait...";  break;
    case lobby::EOperationResult::InternalError:      text = "Internal server error"; break;
    case lobby::EOperationResult::NoFreeSlot:         text = "Game full"; break;
    case lobby::EOperationResult::GameNotFound:       text = "Wrong session id"; break;
    case lobby::EOperationResult::GameStarted:        text = "Game already stared"; break;
    case lobby::EOperationResult::AlreadyInGame:      text = "Already in game"; break;
    case lobby::EOperationResult::RestrictedAccess:   text = "Restricted access"; break;
    case lobby::EOperationResult::RevisionDiffers:    text = "Wrong game version"; break;
  }

  UI::ImageLabel * disp = UI::GetChildChecked<UI::ImageLabel>( pBaseWindow, "JoinResult", true );
  if ( disp )
    disp->SetCaptionTextA( text );
}

} //namespace UI
