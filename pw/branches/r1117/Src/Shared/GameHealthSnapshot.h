#pragma once
// ============================================================================
// In-process game-health snapshot (PLAN_server_pick_ping.md, wave 2).
//
// Every Peered::CommandsScheduler (one per game) measures its own tick
// health and reports it here once per second:
//   work    — average wall time of a tick (Step body), ms;
//   late    — average tick lateness (the slicer ran past the scheduled
//             time — Step receives it as its delta parameter), ms;
//   period  — tick period (the Step return, ms: 10 at 100 Hz, 100 at 10 Hz);
//   players — clients in the game right now (Clients::GetPlayingCount).
//
// The gateway exposes the aggregated values in the web_session_load answer
// (resp/players/delta/period — the back-end server-cost model, wave 1).
// No framework plumbing is needed: the metrics TimeSlicer used to discard
// are taken where the tick actually happens.
//
// Aggregation (recomputed on every report, no history):
//   players = the sum over the reporting games;
//   workPerTick / latePerTick — tick-count-weighted averages;
//   period  — max over the reporting games (0 when none);
//   resp    = EMA( latePerTick + max(0, workPerTick - period/2), alpha=0.2 ),
//             reset to 0 when the last game leaves;
//   delta   = ( workPerTick * games ) / max(1, players ).
// ============================================================================

#include <map>

#include "System/Thread.h"
#include "System/NiTimer.h"

namespace GameHealth
{

  struct Report
  {
    int         players;
    double      workAvgMs;
    double      lateAvgMs;
    double      periodMs;
    int         tickCount;
    timer::Time lastReportAt;

    Report() : players( 0 ), workAvgMs( 0 ), lateAvgMs( 0 ), periodMs( 0 ), tickCount( 0 ), lastReportAt( 0 ) {}
  };

  class Snapshot
  {
  public:
    static Snapshot & Instance();

    // Once per second from CommandsScheduler::Step (its own tick stats).
    void ReportGame( long long serverId, int players, double workAvgMs, double lateAvgMs, double periodMs, int tickCount );

    // The game finished/was destroyed — drop its counters.
    void ForgetGame( long long serverId );

    // Aggregated values for the web_session_load answer. The gateway sends
    // the four fields unconditionally (the back-end treats them as optional —
    // an old gateway simply does not send them).
    void Get( double & respMs, int & players, double & deltaMs, double & periodMs ) const;

  private:
    Snapshot();
    void Recompute( timer::Time now );

    typedef std::map<long long, Report> Reports;
    Reports          reports;
    double           respEma;
    int              aggPlayers;
    double           aggDeltaMs;
    double           aggPeriodMs;
    mutable threading::Mutex mutex;
  };

  inline Snapshot & Snapshot::Instance()
  {
    static Snapshot instance;
    return instance;
  }

  inline Snapshot::Snapshot() : respEma( 0 ), aggPlayers( 0 ), aggDeltaMs( 0 ), aggPeriodMs( 0 )
  {
  }

  inline void Snapshot::ReportGame( long long serverId, int players, double workAvgMs, double lateAvgMs, double periodMs, int tickCount )
  {
    threading::MutexLock lock( mutex );

    timer::Time now = timer::Now();

    Report & rep = reports[serverId];
    rep.players      = players;
    rep.workAvgMs    = workAvgMs;
    rep.lateAvgMs    = lateAvgMs;
    rep.periodMs     = periodMs;
    rep.tickCount    = tickCount;
    rep.lastReportAt = now;

    Recompute( now );
  }

  inline void Snapshot::ForgetGame( long long serverId )
  {
    threading::MutexLock lock( mutex );

    Reports::iterator it = reports.find( serverId );
    if( it != reports.end() )
    {
      reports.erase( it );
      Recompute( timer::Now() );
    }
  }

  inline void Snapshot::Get( double & respMs, int & players, double & deltaMs, double & periodMs ) const
  {
    threading::MutexLock lock( mutex );

    respMs   = respEma;
    players  = aggPlayers;
    deltaMs  = aggDeltaMs;
    periodMs = aggPeriodMs;
  }

  inline void Snapshot::Recompute( timer::Time now )
  {
    // Staleness backstop: a game that stopped reporting (crash without a
    // destructor) must not count forever.
    Reports::iterator st = reports.begin();
    while( st != reports.end() )
    {
      if( now - st->second.lastReportAt > 10.0 )
      {
        st = reports.erase( st );
      }
      else
      {
        ++st;
      }
    }

    if( reports.empty() )
    {
      respEma = 0;
      aggPlayers = 0;
      aggDeltaMs = 0;
      aggPeriodMs = 0;
      return;
    }

    long long totalTicks = 0;
    double workSum = 0;
    double lateSum = 0;
    int totalPlayers = 0;
    double maxPeriod = 0;

    for( Reports::iterator it = reports.begin(); it != reports.end(); ++it )
    {
      const Report & r = it->second;
      totalTicks   += r.tickCount;
      workSum     += r.workAvgMs * r.tickCount;
      lateSum     += r.lateAvgMs * r.tickCount;
      totalPlayers += r.players;
      if( r.periodMs > maxPeriod )
      {
        maxPeriod = r.periodMs;
      }
    }

    double workPerTick = totalTicks > 0 ? workSum / totalTicks : 0;
    double latePerTick = totalTicks > 0 ? lateSum / totalTicks : 0;
    double halfPeriod  = maxPeriod / 2;
    double extra       = workPerTick > halfPeriod ? workPerTick - halfPeriod : 0;
    double newResp     = latePerTick + extra;

    // EMA (alpha 0.2); the first report initializes the value directly.
    respEma = respEma > 0 ? 0.8 * respEma + 0.2 * newResp : newResp;

    int games = (int)reports.size();
    aggPlayers  = totalPlayers;
    aggPeriodMs = maxPeriod;
    aggDeltaMs  = totalPlayers > 0 ? (workPerTick * games) / totalPlayers : workPerTick * games;
  }

}
