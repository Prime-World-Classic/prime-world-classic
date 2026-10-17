#pragma once

#include "AwardsRoll.h"


namespace Peered
{

class WebAwardsJob;

// Battle awards from the web back-end. The Classic cluster has no roll
// service, so a web-session game asks the back-end (httpApi.js, method
// "battleAwards") which awards to give: the back-end rolls them and credits
// them to the accounts once per game. The reliable finish delivery of the
// lobby (finishSession) does the same, so a lost request only costs the
// in-battle display, never the award itself.
//
// The answer goes to the clients through the regular Award() call and shows
// up in the "Awards" block of the final statistics screen. Award types come
// by name: Pearl (Prime crystals), RedPearl, Silver, Resource1..3,
// CustomCurrency (+ currency id), Talent / MarketingEvent (+ persistentId).
//
// The HTTP request is blocking, so it is done by a worker thread shared by
// all games of the process; Step() only polls. While waiting the scheduler
// stays in WaitingRoll (bounded by game_roll_timeout); a failed request
// finishes with empty awards right away.
class WebAwardsRequester : public IAwardsRequester, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2( WebAwardsRequester, IAwardsRequester, BaseObjectMT );

public:
  WebAwardsRequester( NLogg::CChannelLogger * _logStream, TSessionId _serverId, const nstl::string & _sessionToken );
  ~WebAwardsRequester();

  virtual void FillPreGameData( const nstl::vector<ClientInfo> & _clientsInfo, const SAuxData & _auxData );
  virtual void FillPostGameData( lobby::EGameResult::Enum _gameResult, const StatisticService::RPC::SessionClientResults & _gameFinishInfo, const nstl::vector<SClientStatistics> & _clientsStatistics, uint _totalSeconds );
  virtual void Step();
  virtual bool IsAwardsReceived() const { return isAwardsReceived; }
  virtual void GetResults( TUserAwards * _awards ) { *_awards = awards; }

private:
  struct User
  {
    Transport::TClientId  clientId;
    lobby::ETeam::Enum    faction;
  };

  NLogg::CChannelLogger * logStream;
  TSessionId              serverId;
  nstl::string            sessionToken;
  nstl::vector<User>      users;
  int                     winner;   // web team of the winner: 1/2, 0 - none
  nstl::vector<Transport::TClientId> leavers;
  StrongMT<WebAwardsJob>  job;
  TUserAwards             awards;
  bool                    isAwardsReceived;

  void ApplyAnswer( const char * _answer );
};

} // namespace Peered
