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
#if defined(PW_LINUX_DB_BOOTSTRAP)
, linuxProducedStatusCount( 0 )
, linuxConsumedStatusCount( 0 )
#endif
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

#if defined(PW_LINUX_DB_BOOTSTRAP)
void LocalCmdScheduler::QueueLinuxClientStatus(int statusValue)
{
  if ( !gameReady || !currentSegment )
    return;

  // Queue this only after the bootstrap PFWorld has loaded players.
  NCore::ClientStatus & status = currentSegment->statuses.push_back();
  status.clientId = clientId;
  status.status = statusValue;
  status.step = stepIndex + 1;
  ++linuxProducedStatusCount;
}
#endif


CObj<NCore::SyncSegment> LocalCmdScheduler::GetSyncSegment()
{
  if (readySegments.empty())
    return 0;

  CObj<NCore::SyncSegment> result = readySegments.front();
  readySegments.pop_front();
#if defined(PW_LINUX_DB_BOOTSTRAP)
  linuxConsumedStatusCount += result->statuses.size();
#endif
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
    int statusesCount = segmentToWrite->statuses.size();
    if (commandsCount > 0 || statusesCount > 0)
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
      statuses.reserve(statusesCount);
      for (int i = 0; i < statusesCount; ++i)
      {
        const NCore::ClientStatus& clientStatus = segmentToWrite->statuses[i];
        Peered::BriefClientInfo& status = statuses.push_back();
        status.clientId = clientStatus.clientId;
        status.status = static_cast<Peered::Status>(clientStatus.status);
        status.step = clientStatus.step;
      }
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
