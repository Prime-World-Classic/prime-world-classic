#include "stdafx.h"
#include "LobbyFinishDelivery.h"

#include "LobbyLog.h"

#include <Shared/WebJson.h>
#include <Shared/WebRequests.h>

#include <algorithm>
#include <cstdio>
#include <fstream>


namespace lobby
{

// Backoff until the backend acks: fast at first (a restart of the backend is
// usually short), then a slow steady pace. The event is never dropped.
static const float kRetryDelays[]  = { 0.0f, 2.0f, 5.0f, 15.0f, 30.0f, 60.0f, 120.0f };
static const float kRetrySteady    = 300.0f;

static const char * const kJournalTmpSuffix = ".tmp";


void FinishDelivery::Init( const char * _path )
{
  path = ( _path && *_path ) ? _path : "finish_journal.log";
  sequence    = 0;
  initialized = true;

  Load();

  if ( !entries.empty() )
    LOBBY_LOG_MSG( "Finish journal loaded: %d event(s) waiting for delivery", (int)entries.size() );
}


void FinishDelivery::Submit( const Json::Value & _data )
{
  if ( !initialized )
  {
    LOBBY_LOG_ERR( "FinishDelivery::Submit before Init, event dropped" );
    return;
  }

  Entry entry;

  ++sequence;
  entry.id   = NStr::StrFmt( "%lu-%lu", (unsigned long)( timer::Now() * 1000.0 ), sequence );
  entry.data = _data;

  if ( entry.data.isObject() )
    entry.data["eventId"] = Json::Value( entry.id );

  entries.push_back( entry );

  Save();

  LOBBY_LOG_DBG( "Game result queued. event=%s session=%s pending=%d",
    entry.id.c_str(), entry.data.get( "sessionToken", "" ).asCString(), (int)entries.size() );
}


void FinishDelivery::Poll( float _now )
{
  if ( !initialized )
    return;

  bool delivered = false;

  for ( Entries::iterator it = entries.begin(); it != entries.end(); )
  {
    Entry & entry = *it;

    if ( entry.nextAttempt > _now )
    {
      ++it;
      continue;
    }

    if ( Send( entry ) )
    {
      LOBBY_LOG_MSG( "Game result delivered. event=%s session=%s attempts=%d",
        entry.id.c_str(), entry.data.get( "sessionToken", "" ).asCString(), entry.attempts );

      it = entries.erase( it );
      delivered = true;
      continue;
    }

    ++entry.attempts;

    const int   steps = (int)( sizeof( kRetryDelays ) / sizeof( kRetryDelays[0] ) );
    const float delay = ( entry.attempts >= steps ) ? kRetrySteady : kRetryDelays[ entry.attempts - 1 ];

    entry.nextAttempt = _now + delay;

    // Не шумим в логе на каждой попытке: первые три и далее раз в 20.
    if ( entry.attempts <= 3 || entry.attempts % 20 == 0 )
      LOBBY_LOG_ERR( "Game result delivery failed, will retry. event=%s session=%s attempts=%d next_in=%.0fs",
        entry.id.c_str(), entry.data.get( "sessionToken", "" ).asCString(), entry.attempts, delay );

    ++it;
  }

  if ( delivered )
    Save();
}


bool FinishDelivery::Send( const Entry & _entry ) const
{
  const std::string response = WebSessionRequest( "finishSession", _entry.data );

  if ( response.empty() )
    return false;   // backend unreachable

  const Json::Value parsed = WebSession::ParseJson( response.c_str() );

  if ( parsed.empty() )
    return false;   // broken answer — treat as not delivered

  const Json::Value error = parsed.get( "error", "ERROR" );

  return error.isString() && error.asString().empty();
}


void FinishDelivery::Load()
{
  entries.clear();

  std::ifstream file( path.c_str() );
  if ( !file.is_open() )
    return;

  std::string line;
  while ( std::getline( file, line ) )
  {
    if ( line.empty() )
      continue;

    const Json::Value event = WebSession::ParseJson( line.c_str() );

    if ( event.empty() || !event.isObject() || !event.isMember( "data" ) )
    {
      LOBBY_LOG_ERR( "Finish journal: bad line skipped in %s", path.c_str() );
      continue;
    }

    Entry entry;
    entry.id        = event.get( "id", "" ).asString();
    entry.data      = event.get( "data", Json::Value( Json::objectValue ) );
    entry.nextAttempt = 0.0f;   // сразу первая попытка
    entry.attempts  = 0;

    entries.push_back( entry );
  }
}


// Only unacknowledged events are kept; the file is rewritten atomically.
void FinishDelivery::Save() const
{
  const std::string tmp = path + kJournalTmpSuffix;

  {
    std::ofstream file( tmp.c_str(), std::ios::out | std::ios::trunc );
    if ( !file.is_open() )
    {
      LOBBY_LOG_ERR( "Finish journal: cannot write %s", tmp.c_str() );
      return;
    }

    Json::FastWriter writer;

    for ( Entries::const_iterator it = entries.begin(); it != entries.end(); ++it )
    {
      Json::Value event;
      event["id"]   = Json::Value( it->id );
      event["data"] = it->data;

      file << writer.write( event );
    }
  }

  std::remove( path.c_str() );   // rename() over an existing file is not portable

  if ( std::rename( tmp.c_str(), path.c_str() ) != 0 )
    LOBBY_LOG_ERR( "Finish journal: cannot replace %s", path.c_str() );
}

} //namespace lobby
