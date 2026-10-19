#include "stdafx.h"
#include "WebAwardsRoll.h"

#include "System/JobThread.h"
#include "System/HPTimer.h"
#include "System/Crc32Checksum.h"

#include <Shared/WebJson.h>
#include <Shared/WebRequests.h>

#include <list>
#include <string>


namespace Peered
{

// Attempts per request (a back-end restart is short) and the pause between
// them. After kJobLifetime the scheduler has stopped waiting
// (game_roll_timeout), the awards are credited by finishSession anyway, so
// the remaining attempts are dropped.
static const int          kAttempts       = 3;
static const unsigned     kRetryPauseMs   = 1000;
static const timer::Time  kJobLifetime    = 25.0;


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// One request: built by the requester, executed by the worker thread.
class WebAwardsJob : public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_1( WebAwardsJob, BaseObjectMT );

public:
  WebAwardsJob( const std::string & _body, const WebSessionEndpoint & _endpoint ) :
  body( _body ),
  host( _endpoint.host ),
  port( _endpoint.port ),
  deadline( timer::Now() + kJobLifetime ),
  done( false ),
  cancelled( false )
  {}

  // worker thread
  void Run()
  {
    std::string result;

    for ( int attempt = 0; attempt < kAttempts && result.empty(); ++attempt )
    {
      if ( attempt )
        threading::Sleep( kRetryPauseMs );

      if ( IsCancelled() || timer::Now() > deadline )
        break;

      result = WebSessionHttpPost( host, port, body );
    }

    threading::MutexLock lock( mutex );
    answer = result;
    done = true;
  }

  // false while the request is in progress; an empty answer - no connection
  bool TakeAnswer( std::string * _answer )
  {
    threading::MutexLock lock( mutex );
    if ( !done )
      return false;
    *_answer = answer;
    return true;
  }

  void Cancel()
  {
    threading::MutexLock lock( mutex );
    cancelled = true;
  }

private:
  const std::string   body;
  const std::string   host;
  const int           port;
  const timer::Time   deadline;

  threading::Mutex    mutex;
  std::string         answer;
  bool                done;
  bool                cancelled;

  bool IsCancelled()
  {
    threading::MutexLock lock( mutex );
    return cancelled;
  }
};


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// One worker thread for all games of the process: the requests are rare
// (one per finished game) and short.
class WebAwardsWorker : public threading::IThreadJob, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2( WebAwardsWorker, threading::IThreadJob, BaseObjectMT );

public:
  void Push( WebAwardsJob * _job )
  {
    threading::MutexLock lock( mutex );
    queue.push_back( _job );
  }

  virtual void Work( volatile bool & isRunning )
  {
    while ( isRunning )
    {
      StrongMT<WebAwardsJob> next;
      {
        threading::MutexLock lock( mutex );
        if ( !queue.empty() )
        {
          next = queue.front();
          queue.pop_front();
        }
      }

      if ( next )
        next->Run();
      else
        threading::Sleep( 20 );
    }
  }

private:
  threading::Mutex                    mutex;
  std::list<StrongMT<WebAwardsJob> >  queue;
};


struct WebAwardsWorkerHost
{
  StrongMT<WebAwardsWorker>       worker;
  StrongMT<threading::JobThread>  thread;
};

static threading::Mutex       s_workerLock;
static WebAwardsWorkerHost *  s_workerHost = 0;   // started on the first request, lives until the process exits

static void SubmitJob( WebAwardsJob * _job )
{
  threading::MutexLock lock( s_workerLock );

  if ( !s_workerHost )
  {
    s_workerHost = new WebAwardsWorkerHost;
    s_workerHost->worker = new WebAwardsWorker;
    s_workerHost->thread = new threading::JobThread( s_workerHost->worker, "WebAwards" );
  }

  s_workerHost->worker->Push( _job );
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// {"type":"Pearl","count":2}, {"type":"CustomCurrency","currency":"<id>","count":1},
// {"type":"MarketingEvent","id":"<persistentId>","count":1}
static bool ParseAward( const Json::Value & _item, roll::SAwardInfo & _award )
{
  if ( !_item.isObject() )
    return false;

  const std::string type = _item.get( "type", Json::Value( "" ) ).asString();

  _award.count = (float)_item.get( "count", Json::Value( 0 ) ).asDouble();
  _award.isOwnedByPlayer = true;

  if ( type == "Pearl" )
    _award.type = roll::EAwardType::Pearl;
  else if ( type == "RedPearl" )
    _award.type = roll::EAwardType::RedPearl;
  else if ( type == "Silver" )
    _award.type = roll::EAwardType::Silver;
  else if ( type == "Resource1" )
    _award.type = roll::EAwardType::Resource1;
  else if ( type == "Resource2" )
    _award.type = roll::EAwardType::Resource2;
  else if ( type == "Resource3" )
    _award.type = roll::EAwardType::Resource3;
  else if ( type == "CustomCurrency" )
  {
    _award.type = roll::EAwardType::CustomCurrency;
    _award.customCurrencyId = _item.get( "currency", Json::Value( "" ) ).asString().c_str();
    if ( _award.customCurrencyId.empty() )
      return false;
  }
  else if ( type == "Talent" || type == "MarketingEvent" )
  {
    // the client looks them up by crc32 of the persistentId (PFResourcesCollection)
    const std::string persistentId = _item.get( "id", Json::Value( "" ) ).asString();
    if ( persistentId.empty() )
      return false;
    _award.type = ( type == "Talent" ) ? roll::EAwardType::Talent : roll::EAwardType::MarketingEvent;
    _award.id = (int)Crc32Checksum().AddString( persistentId.c_str() ).Get();
  }
  else
    return false;

  return _award.count > 0;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
WebAwardsRequester::WebAwardsRequester( NLogg::CChannelLogger * _logStream, TSessionId _serverId, const nstl::string & _sessionToken ) :
logStream( _logStream ),
serverId( _serverId ),
sessionToken( _sessionToken ),
winner( 0 ),
isAwardsReceived( false )
{
}



WebAwardsRequester::~WebAwardsRequester()
{
  if ( job )
    job->Cancel();
}



void WebAwardsRequester::FillPreGameData( const nstl::vector<ClientInfo> & _clientsInfo, const SAuxData & _auxData )
{
  users.clear();
  users.reserve( _auxData.users.size() );

  for ( int i = 0; i < _auxData.users.size(); ++i )
  {
    User & user = users.push_back();
    user.clientId = _auxData.users[i].clientId;
    user.faction = _auxData.users[i].faction;
  }
}



// Same game result as in the lobby finish event (LobbyServerNode.cpp,
// BuildFinishEvent): the back-end must get identical win/afk on both paths.
void WebAwardsRequester::FillPostGameData( lobby::EGameResult::Enum _gameResult, const StatisticService::RPC::SessionClientResults & _gameFinishInfo, const nstl::vector<SClientStatistics> & _clientsStatistics, uint _totalSeconds )
{
  if ( job || isAwardsReceived )
    return;

  winner = (int)_gameFinishInfo.sideWon + 1;

  Json::Value afk( Json::arrayValue );
  leavers.clear();

  if ( _gameFinishInfo.sideWon != -1 )
  {
    for ( int i = 0; i < _clientsStatistics.size(); ++i )
    {
      const SClientStatistics & stat = _clientsStatistics[i];
      if ( stat.clientState != EGameFinishClientState::FinishedGame )
      {
        afk.append( Json::Value( (int)stat.clientId ) );
        leavers.push_back( stat.clientId );
      }
    }
  }

  // no winner - no awards, nothing to ask
  if ( winner != 1 && winner != 2 )
  {
    localLog( logStream, NLogg::LEVEL_DEBUG ).Trace(
      "GameServer: No winner, finishing without awards (sid=%016x result=%d)", serverId, (int)_gameResult );
    isAwardsReceived = true;
    return;
  }

  Json::Value data( Json::objectValue );
  data["sessionToken"] = Json::Value( sessionToken.c_str() );
  data["win"] = Json::Value( winner );
  data["afk"] = afk;

  job = new WebAwardsJob( BuildSessionRequest( "battleAwards", data ), GetWebSessionEndpoint() );
  SubmitJob( job );

  localLog( logStream, NLogg::LEVEL_DEBUG ).Trace(
    "GameServer: Awards requested from the back-end (sid=%016x win=%d leavers=%d)", serverId, winner, leavers.size() );
}



void WebAwardsRequester::Step()
{
  if ( isAwardsReceived || !job )
    return;

  std::string answer;
  if ( !job->TakeAnswer( &answer ) )
    return;

  job = 0;

  ApplyAnswer( answer.c_str() );
  isAwardsReceived = true;
}



// {"error":"", "awards":[{"uid":<user id>, "items":[{"type":"Pearl","count":2}]}]}
// Any failure finishes the game without awards (they are credited anyway).
void WebAwardsRequester::ApplyAnswer( const char * _answer )
{
  awards.clear();

  if ( !_answer || !*_answer )
  {
    localLog( logStream, NLogg::LEVEL_WARNING ).Trace(
      "GameServer: Back-end is unreachable, finishing without awards (sid=%016x)", serverId );
    return;
  }

  const Json::Value parsed = WebSession::ParseJson( _answer );
  const Json::Value error = parsed.isObject() ? parsed.get( "error", Json::Value( "no error field" ) ) : Json::Value( "broken answer" );

  if ( !error.isString() || !error.asString().empty() )
  {
    localLog( logStream, NLogg::LEVEL_WARNING ).Trace(
      "GameServer: Back-end refused awards, finishing without them (sid=%016x error=%s)", serverId, error.isString() ? error.asString().c_str() : "?" );
    return;
  }

  const Json::Value list = parsed.get( "awards", Json::Value( Json::arrayValue ) );
  if ( !list.isArray() )
    return;

  int total = 0;

  for ( Json::Value::ArrayIndex i = 0; i < list.size(); ++i )
  {
    const Json::Value & entry = list[i];
    if ( !entry.isObject() )
      continue;

    roll::SUserAward userAward;
    userAward.userId = (roll::UserId)entry.get( "uid", Json::Value( 0 ) ).asInt();

    const Json::Value items = entry.get( "items", Json::Value( Json::arrayValue ) );
    if ( !items.isArray() )
      continue;

    for ( Json::Value::ArrayIndex j = 0; j < items.size(); ++j )
    {
      roll::SAwardInfo award;
      if ( ParseAward( items[j], award ) )
        userAward.awards.push_back( award );
    }

    if ( !userAward.userId || userAward.awards.empty() )
      continue;

    for ( int u = 0; u < users.size(); ++u )
      if ( users[u].clientId == userAward.userId )
        userAward.playerWin = ( (int)users[u].faction + 1 == winner );

    for ( int l = 0; l < leavers.size(); ++l )
      if ( leavers[l] == userAward.userId )
        userAward.playerLeaver = true;

    total += userAward.awards.size();
    awards.push_back( userAward );
  }

  localLog( logStream, NLogg::LEVEL_DEBUG ).Trace(
    "GameServer: Awards received from the back-end for %d users, %d items (sid=%016x)", awards.size(), total, serverId );
}

} // namespace Peered
