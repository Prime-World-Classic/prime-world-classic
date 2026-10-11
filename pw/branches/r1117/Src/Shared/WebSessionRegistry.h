#pragma once
// ============================================================================
// In-process web-session registry (server side).
//
// Replaces the back-end 'web_session' table. The back-end pushes each web
// session to the game server exactly once (HttpGateway method
// "web_session_register") and waits for the ACK before handing the launch
// protocols to the clients. From that moment the session lives here, in the
// UniServerApp process, visible to every service instance:
//   - newlogin authorizes players against it (local playerKey check,
//     Shared/Sha256.h) and answers LoginReply with the match metadata;
//   - the lobby builds the custom game from it (fake connections, hero /
//     team / party assignment).
// No HTTP is involved on either path.
//
// Lifetime: TTL backstop (a match is far shorter) + MarkFinished on game
// end. Records are small (a few KB) and short-lived, a process-global map
// with a mutex is enough.
// ============================================================================

#include <string>
#include <vector>
#include <map>

#include "System/NiTimer.h"
#include "System/Thread.h"
#include "json/json.h"

#include "Shared/WebSessionParse.h"   // WebSession::Player, ParsePlayer

namespace WebSession
{

  struct Record
  {
    std::string         mapId;
    int                 mode;
    std::vector<Player> players;
    timer::Time         createdAt;
    bool                started;
    bool                finished;

    Record() : mode( 0 ), createdAt( 0 ), started( false ), finished( false ) {}
  };

  class ERegistryResult
  {
  public:
    enum Enum { Ok, InvalidPayload };
  };

  class Registry
  {
  public:
    static Registry & Instance();

    // token: 32 hex chars; players: the back-end 'players' JSON array (the
    // format the old 'usersData' had). Registering an existing token keeps
    // the first record and still returns Ok: the back-end retries the push
    // until it sees the ACK, and a duplicate push must be acked, not failed.
    ERegistryResult::Enum Register( const std::string & token, const std::string & mapId, int mode, const Json::Value & players );

    // Fills 'out' with a copy of the record: it may be removed (TTL) at any
    // moment, so callers must not keep pointers into the registry.
    bool Find( const std::string & token, Record & out );

    void MarkStarted( const std::string & token );
    void MarkFinished( const std::string & token );

    // PLAN_game_server_load_balancing.md — the load metric the back-end sorts
    // the pool by (served by the gateway "web_session_load" query): in-
    // progress games (started && !finished). Registered-but-not-started and
    // finished records do not count.
    size_t ActiveCount();

    size_t Size();

  private:
    void Sweep( timer::Time now );

    typedef std::map<std::string, Record> Records;
    Records          records;
    threading::Mutex mutex;
  };


  // A match is far shorter than this; abandoned sessions die quietly.
  static timer::Time RegistryTtl()
  {
    return 7200.0;   // 2 h
  }


  inline Registry & Registry::Instance()
  {
    static Registry instance;
    return instance;
  }

  inline ERegistryResult::Enum Registry::Register( const std::string & token, const std::string & mapId, int mode, const Json::Value & players )
  {
    if ( token.size() != 32 || mapId.empty() || !players.isArray() || players.empty() )
      return ERegistryResult::InvalidPayload;

    std::vector<Player> parsed;
    parsed.reserve( players.size() );
    for ( size_t i = 0; i < players.size(); ++i )
    {
      Player player;
      if ( !ParsePlayer( players[(int)i], player ) )
        return ERegistryResult::InvalidPayload;
      parsed.push_back( player );
    }

    threading::MutexLock lock( mutex );
    Sweep( timer::Now() );

    Records::iterator it = records.find( token );
    if ( it != records.end() )
      return ERegistryResult::Ok;   // idempotent: the back-end retries until the ACK

    Record record;
    record.mapId     = mapId;
    record.mode      = mode;
    record.players   = parsed;
    record.createdAt = timer::Now();
    records[token]   = record;
    return ERegistryResult::Ok;
  }

  inline bool Registry::Find( const std::string & token, Record & out )
  {
    threading::MutexLock lock( mutex );

    Records::iterator it = records.find( token );
    if ( it == records.end() )
      return false;

    out = it->second;
    return true;
  }

  inline void Registry::MarkStarted( const std::string & token )
  {
    threading::MutexLock lock( mutex );

    Records::iterator it = records.find( token );
    if ( it != records.end() )
      it->second.started = true;
  }

  inline void Registry::MarkFinished( const std::string & token )
  {
    threading::MutexLock lock( mutex );

    Records::iterator it = records.find( token );
    if ( it != records.end() )
      it->second.finished = true;
  }

  inline size_t Registry::ActiveCount()
  {
    threading::MutexLock lock( mutex );

    size_t count = 0;
    for ( Records::const_iterator it = records.begin(); it != records.end(); ++it )
      if ( it->second.started && !it->second.finished )
        ++count;
    return count;
  }

  inline size_t Registry::Size()
  {
    threading::MutexLock lock( mutex );
    return records.size();
  }

  // Called with the lock held.
  inline void Registry::Sweep( timer::Time now )
  {
    for ( Records::iterator it = records.begin(); it != records.end(); )
    {
      if ( it->second.createdAt + RegistryTtl() < now )
        it = records.erase( it );
      else
        ++it;
    }
  }

} //namespace WebSession
