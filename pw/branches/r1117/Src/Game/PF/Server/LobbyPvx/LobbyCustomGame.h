#ifndef LOBBYCUSTOMGAME_H_INCLUDED
#define LOBBYCUSTOMGAME_H_INCLUDED

#include "CommonTypes.h"
#include "LobbyConfig.h"
#include "../PF_GameLogic/WebLauncher.h"


namespace mmaking
{
  class IHeroesTable;
}


namespace Peered
{
  struct ClientInfo;
}


namespace NDb
{
  // C++17 (GCC): unscoped-enum forward declaration is ill-formed without a prior
  // definition (MSVC accepted it as an extension). The real enum is not defined
  // anywhere in this tree and no value is ever used — keep the single-value
  // definition as the minimal valid stand-in for the original `enum EMmAlgorithm;`.
  enum EMmAlgorithm { EMmAlgorithm_None };
}

namespace lobby
{

class ServerConnection;


namespace ECustomGameState
{
  enum Enum { Lobby, Starting, Empty };
};



struct CustomGameMember
{
  StrongMT<ServerConnection>    player;
  SPlayerGameContext            context;
  EGameMemberReadiness::Enum    customLobbyReadiness;
  uint                          partyId;

  CustomGameMember() :
  customLobbyReadiness( EGameMemberReadiness::NotReady ), partyId(0U)
  {}

  CustomGameMember( ServerConnection * plr, const SPlayerGameContext & ctx ) :
  player( plr ), context( ctx ), customLobbyReadiness( EGameMemberReadiness::NotReady ), partyId(0U)
  {}
};



class CustomGame : public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_1( CustomGame, BaseObjectMT );

public:
  CustomGame( IConfigProvider * _cfg, TGameId _id, const SGameParameters & _params, mmaking::IHeroesTable * _heroes, int autostartPlayers );

  ~CustomGame();

  TGameId Id() const { return id; }
  ECustomGameState::Enum State() const { return state; };

  const SGameParameters & Params() const { return params; }
  const vector<CustomGameMember> & Players() const { return players; }

  void Poll( timer::Time now );

  EOperationResult::Enum SetupCustom( ServerConnection * firstPlayer );
  EOperationResult::Enum AddPlayerToCustomLobby( ServerConnection * player );

  void RemovePlayer( ServerConnection * _player );

  void ChangeCustomGameSettings( ServerConnection * player, ETeam::Enum team, ETeam::Enum faction, const string & heroId );
  void SetPlayerReady( ServerConnection * player, EGameMemberReadiness::Enum readiness );

  void SetupGameStartInfo( vector<Peered::ClientInfo> & _gameServerData, TGameLineUp & _lineup );

  void SetDeveloperParty( ServerConnection * player, int party );
  void StartGame();

  void SetSessionToken(const char* _sessionToken) { sessionToken = _sessionToken; }
  const char* GetSessionToken() { return sessionToken.c_str(); }

  WebUsersDataMap                 playersUserData;

private:
  const TGameId                   id;
  StrongMT<IConfigProvider>       config;
  SGameParameters                 params;
  ECustomGameState::Enum          state;
  vector<CustomGameMember>        players;
  StrongMT<mmaking::IHeroesTable> heroes;
  int                             autostartPlayers;
  bool                            eliseevHackForPve;
  nstl::string                    sessionToken;

  void ChangeState( ECustomGameState::Enum _st ) { state = _st; }
  int FindPlayer( ServerConnection * player );
  int AddPlayerInternal( ServerConnection * player, ETeam::Enum team, const char * heroId );
  void FindOutNewPlayerInfo( string & heroId, ETeam::Enum & teamId, int teamSizes[2] );
  void ComplementWithLegacyAi( TGameLineUp & _lineup );
  void BalanceCustomGame();

  void BroadcastPlayerSettings( size_t playerIndex );
  void BroadcastPlayerLeaveCustom( Transport::TClientId playerClientId );
  void BroadcastCustomReadiness( Transport::TClientId playerClientId, bool isReady );
  void DropReadinessAndBroadcast();
};

} //namespace lobby

#endif //LOBBYCUSTOMGAME_H_INCLUDED
