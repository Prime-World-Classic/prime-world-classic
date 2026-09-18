#pragma once

#include <json/json.h>

#include <list>
#include <string>


namespace lobby
{

// Reliable delivery of the game results to the backend.
//
// The standalone synchronizer used to provide this guarantee with an endless
// retry loop; once it is gone the guarantee belongs to the lobby. An event is
// written to a journal file before the first attempt, so a lobby crash or a
// backend outage cannot lose it, and it is retried until the backend acks.
// The backend is idempotent (web_session flags + eventId), so repeats are safe.
// See PLAN_remove_synchronizer.md §3.4.
class FinishDelivery
{
public:
  // Loads the journal (if any) and schedules the unacknowledged events.
  void Init( const char * _path );

  // Appends the event to the journal; the first attempt happens in a Poll.
  void Submit( const Json::Value & _data );

  // Retries the events whose time has come.
  void Poll( float _now );

  size_t Pending() const { return entries.size(); }

private:
  struct Entry
  {
    Entry() : nextAttempt( 0 ), attempts( 0 ) {}

    std::string id;           // eventId, echoed to the backend for dedup
    Json::Value data;         // payload of the finishSession method
    float       nextAttempt;
    int         attempts;
  };

  typedef std::list<Entry> Entries;

  bool Send( const Entry & _entry ) const;
  void Load();
  void Save() const;

  std::string     path;
  Entries         entries;
  unsigned long   sequence;
  bool            initialized;
};

} //namespace lobby
