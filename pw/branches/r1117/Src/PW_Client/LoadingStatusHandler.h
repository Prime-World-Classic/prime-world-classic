#pragma once

#include "GameStatStatus.h"
#include "Network/LoginTypes.h"
#include "Game/PF/Server/LobbyPvx/CommonTypes.h"
#if defined(PW_LINUX_DB_BOOTSTRAP) && !defined(NEW_LOBBYCLIENTBASE_H_INCLUDED)
#include "Game/PF/Client/LobbyPvx/LobbyClientStatusCompat.h"
#else
#include "Game/PF/Client/LobbyPvx/LobbyClientBase.h"
#endif

namespace NDb
{
  struct DBUIData;
}

namespace Game
{
  class LoadingFlashInterface;

  namespace EReplayStatus
  {
    enum Enum
    {
      Failure,
      WrongFormat,
      WrongVersion
    };
  }
}

namespace NGameX
{

  class LoadingStatusHandler : public BaseObjectST
{
  NI_DECLARE_REFCOUNT_CLASS_1( LoadingStatusHandler, BaseObjectST)
public:
  LoadingStatusHandler(const NDb::DBUIData * _uiData);

  void SetFlashInterface(Game::LoadingFlashInterface * _flashInterface);
  void OnLoginStatus( Login::ELoginResult::Enum loginStatus );
  void OnGameStatStatus( Game::EGameStatStatus::Enum gameStatStatus );
  void OnLobbyStatus( lobby::EClientStatus::Enum lobbyStatus );
  void OnLobbyInGameStatus( lobby::EOperationResult::Enum inGameStatus );
  void OnReplayStatus( Game::EReplayStatus::Enum replayStatus );
  const string& GetLastStatusId() const { return lastStatus; }
  const string& GetPendingStatusId() const { return pendingStatus; }

private:
  void SetStatusText( const char * textId );
  wstring FindLocalizedString( const char * textId );

  Weak<Game::LoadingFlashInterface> flashInterface;
  NDb::Ptr<NDb::DBUIData>         uiData;
  string                            lastStatus;
  string                            pendingStatus;
};

}
