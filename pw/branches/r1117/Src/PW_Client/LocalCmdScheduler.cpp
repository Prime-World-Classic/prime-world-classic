#include "stdafx.h"
#include "LocalCmdScheduler.h"
#include "Core/GameCommand.h"
#include "Game/PF/Client/LobbyPvx/NewReplay.h"
#include "HybridServer/Peered.h"
#include "Core/CommandSerializer.h"


namespace Game
{

const timer::Time s_StepTime = 0.1;

LocalCmdScheduler::LocalCmdScheduler( int _clientId ) :
clientId( _clientId ),
localTime( 0 ),
gameReady( false ),
stepIndex( 0 ),
nextStepTime( 0 )
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
  if (readySegments.empty())
    return 0;

  CObj<NCore::SyncSegment> result = readySegments.front();
  readySegments.pop_front();
  return result;
}



int LocalCmdScheduler::GetNextStep( bool warnIfNoSegments ) const
{
  (void)warnIfNoSegments;
  return readySegments.empty() ? NCore::INVALID_STEP : readySegments.back()->step;
}



void LocalCmdScheduler::Step( float dt )
{
  localTime += dt;

  if ( localTime < nextStepTime )
    return;

  nextStepTime = localTime + s_StepTime; //OR nextStepTime += 0.1 - to catchup in case of frame rate drops

  if ( !gameReady )
    return;

  ++stepIndex;
  readySegments.push_back(currentSegment);
  CObj<NCore::SyncSegment> segmentToWrite = currentSegment;
  currentSegment = new NCore::SyncSegment( stepIndex );

  if (StrongMT<NCore::ReplayWriter> lockedReplayWriter = replayWriter.Lock())
  {
    int commandsCount = segmentToWrite->commands.size();
    if (commandsCount > 0)
    {
      vector<MemoryStream> commandsAsStreams(commandsCount);
      vector<rpc::MemoryBlock> commands(commandsCount);
      for (int i = 0; i < commandsCount; ++i)
      {
        WriteCommandToStream( segmentToWrite->commands[i], &commandsAsStreams[i], 0 );
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
