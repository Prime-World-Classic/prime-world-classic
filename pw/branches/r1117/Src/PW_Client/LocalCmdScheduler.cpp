#include "stdafx.h"
#include "LocalCmdScheduler.h"
#include "Core/GameCommand.h"
#include "Game/PF/Client/LobbyPvx/NewReplay.h"
#include "HybridServer/Peered.h"
#include "Core/CommandSerializer.h"


namespace Game
{

LocalCmdScheduler::LocalCmdScheduler( int _clientId, int _stepLengthMs ) :
clientId( _clientId ),
localTime( 0 ),
gameReady( false ),
stepIndex( 0 ),
nextStepTime( 0 ),
stepTime( Max( _stepLengthMs, 1 ) / 1000.0 )
{
  currentSegment = new NCore::SyncSegment( stepIndex );
}



void LocalCmdScheduler::SendMessage( CObjectBase * pMsg, bool isPlayerCommand )
{
  if ( !gameReady )
    return;

  NCore::PackedWorldCommand * worldCmd = dynamic_cast<NCore::PackedWorldCommand *>( pMsg );
  NI_VERIFY( worldCmd, "Only PackedWorldCommand is allowed in local mode", return );
  NI_VERIFY( currentSegment, "", return );

  currentSegment->commands.push_back( worldCmd );
}



CObj<NCore::SyncSegment> LocalCmdScheduler::GetSyncSegment()
{
  CObj<NCore::SyncSegment> result = nextSegment;
  nextSegment = 0;
  return result;
}



int LocalCmdScheduler::GetNextStep( bool warnIfNoSegments ) const
{
  if ( !nextSegment )
    return -1;

  return stepIndex;
}



void LocalCmdScheduler::Step( float dt )
{
  localTime += dt;

  if ( localTime < nextStepTime )
    return;

  // Previous segment was not taken by the transceiver yet (it runs on its own clock and may lag a bit).
  // Do not overwrite it - otherwise all player commands from that segment (e.g. move clicks) are silently lost.
  // Keep collecting commands into currentSegment and rotate right after the transceiver takes nextSegment.
  if ( gameReady && nextSegment )
    return;

  // Keep a steady step rate (stepTime). 'localTime + 0.1' restarted the schedule from the current frame,
  // so every step lasted 0.1 s plus part of a frame (~0.108 s on average at 60 fps, uneven 6/7 frames):
  // the game ran slower than real time and units visibly stuttered between steps.
  nextStepTime += stepTime;
  if ( nextStepTime < localTime ) // more than a whole step late (hitch, pause): resync instead of catching up
    nextStepTime = localTime;

  if ( !gameReady )
    return;

  ++stepIndex;
  nextSegment = currentSegment;
  currentSegment = new NCore::SyncSegment( stepIndex );

  if (StrongMT<NCore::ReplayWriter> lockedReplayWriter = replayWriter.Lock())
  {
    int commandsCount = nextSegment->commands.size();
    if (commandsCount > 0)
    {
      vector<MemoryStream> commandsAsStreams(commandsCount);
      vector<rpc::MemoryBlock> commands(commandsCount);
      for (int i = 0; i < commandsCount; ++i)
      {
        WriteCommandToStream( nextSegment->commands[i], &commandsAsStreams[i], 0 );
        commands[i].memory = static_cast<void *>(commandsAsStreams[i].GetBuffer());
        commands[i].size = commandsAsStreams[i].GetPosition();
      }
      vector<Peered::BriefClientInfo> statuses;
      lockedReplayWriter->WriteStepData(stepIndex, commands, statuses);
    }
  }
}

void LocalCmdScheduler::OnCombatScreenStarted( const NGameX::ReplayInfo & _replayInfo )
{
  if (StrongMT<NCore::ReplayWriter> lockedReplayWriter = replayWriter.Lock())
    lockedReplayWriter->WriteStartGameInfo(_replayInfo);
}

void LocalCmdScheduler::OnVictory(const StatisticService::RPC::SessionClientResults & _sessionResults, const NGameX::ReplayInfo & _replayInfo)
{
  if (StrongMT<NCore::ReplayWriter> lockedReplayWriter = replayWriter.Lock())
  {
    lockedReplayWriter->WriteFinishGame(stepIndex, _sessionResults, _replayInfo );
    replayWriter = 0;
  }
}

} //namespace Game
