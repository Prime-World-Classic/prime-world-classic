#include "stdafx.h"
#include "ReplayTransceiver.h"

#include "Core/Replay.h"
#include "Core/WorldBase.h"
#include "Core/WorldCommand.h"

#include "System/ChunklessSaver.h"
#include "System/SyncProcessorState.h"
#include "System/ConfigFiles.h"
#include "System/HPTimer.h"
#include "System/JobThread.h"

#include "PF_GameLogic/GameMaps.h"
#include "PF_GameLogic/IAdventureScreen.h"
#include "PF_GameLogic/StringExecutor.h" // ExecutableFloatString for the DB headers of PFWorld.h
#include "PF_GameLogic/PFWorld.h"
#include "PF_GameLogic/PFAIWorld.h"
#include "PF_GameLogic/PFAIContainer.h"
#include "PF_GameLogic/WorldChecker.h"

#include "Client/MainTimer.h"
#include "HybridServer/PeeredTypes.h"
#include "Network/LoginClientVersion.h"
#include "PW_Client/LoadingStatusHandler.h"
#include "Version.h"


namespace
{
DEV_VAR_STATIC int g_saveWorldForReplayStep = NCore::INVALID_STEP;
DEV_VAR_STATIC int g_skipAllUntilReplayStep = NCore::INVALID_STEP;

const static float g_replayMsgTime = 3000.0f;
const static float g_replaySpeedIncStep = 2.0f;
const static float g_replaySpeedDecStep = 0.5f;

static float g_replaySpeedIncLimit = 8.0f;
static float g_replaySpeedDecLimit = 0.25f;

// Seeking and rewinding
static float g_replaySeekStepSeconds = 10.0f;       // left/right arrows
static float g_replaySnapshotInterval = 30.0f;      // seconds of game time between world snapshots
static int   g_replaySnapshotMemoryMb = 256;        // compressed snapshots are thinned out above this
static float g_replaySeekFrameBudget = 0.04f;       // seconds of simulation per frame while seeking

const static int MAX_STEPS_PER_FRAME = 16;          // normal playback at high speed / low FPS
}


namespace NWorld
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ReplayStorage2::ReplayStorage2(NCore::ReplayBufferMode mode,
                               const char * fileName,
                               NWorld::IMapCollection * _mapCollection,
                               NGameX::LoadingStatusHandler * _loadingStatusHandler)
  : mapCollection(_mapCollection)
  , loadingStatusHandler(_loadingStatusHandler)
  , dataStart(-1)
  , startStep(NCore::INVALID_STEP)
  , currentStep(NCore::INVALID_STEP)
  , nextStep(NCore::INVALID_STEP)
  , isFinished(false)
{
  if (mode == NCore::REPLAY_BUFFER_READ)
  {
    replayFile = new FileStream( fileName, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING );
    NI_ASSERT(replayFile->IsOk(), NStr::StrFmt( "Cannot open file \"%s\"", fileName ));
    SetLoadingStatus(Game::EReplayStatus::Failure);
  }
  else
  {
    NI_ALWAYS_ASSERT( "ReplayStorage2 don't support replay writing!" );
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool ReplayStorage2::GetNextSegment( NCore::SyncSegment & segOut )
{
  NI_PROFILE_FUNCTION;

  if (isFinished)
    return false;

  ushort commandsCount;
  ushort statusesCount;

  if (startStep == NCore::INVALID_STEP)
  {
    replayFile->Read( &startStep, sizeof(int) );
    replayFile->Read( &nextStep, sizeof(int) );
    currentStep = startStep;
  }
  else
  {
    ++currentStep;
  }

  segOut.commands.clear();
  segOut.statuses.clear();
  segOut.step = (uint)currentStep;

  if (currentStep < nextStep)
    return true;

  replayFile->Read( &commandsCount, sizeof(ushort) );
  replayFile->Read( &statusesCount, sizeof(ushort) );

  segOut.commands.reserve(commandsCount);

  for (int i = 0; i < commandsCount; ++i)
  {
    ushort size;
    replayFile->Read( &size, sizeof(ushort) );
    const int endPos = replayFile->GetPosition() + size;
    int id = 0;
    replayFile->Read( &id, sizeof( id ) );
    CObjectBase * command = NObjectFactory::MakeObject( id );
    NI_VERIFY( command, NStr::StrFmt( "Can not create command object with id = 0x%08X!", id ),
                  isFinished = true; return false; );
    CObj<IBinSaver> saver = CreateChunklessSaver( replayFile, 0, true );
    saver->AddPolymorphicBase( 1, command );
    NI_VERIFY( replayFile->GetPosition() == endPos, "Command's data size mismatch!",
                  isFinished = true; return false; );
    // Dynamic cast will return null for loading status commands - they should be ignored
    if (NCore::PackedWorldCommand * packedCommand = dynamic_cast<NCore::PackedWorldCommand *>(command) )
      segOut.commands.push_back( packedCommand );
  }

  segOut.statuses.reserve(statusesCount);

  for (int i = 0; i < statusesCount; ++i)
  {
    ushort size;
    replayFile->Read( &size, sizeof(ushort) );
    NI_VERIFY( sizeof(NCore::ClientStatus) == size, "Wrong status data in replay!",
                  isFinished = true; return false; );
    NCore::ClientStatus & status = segOut.statuses.push_back();
    replayFile->Read( &status, sizeof(NCore::ClientStatus) );
    status.step = status.step - startStep;  // Translate server step to world step
  }

  if (replayFile->GetPosition() == replayFile->GetSize())
  {
    isFinished = true;
    return false;
  }

  replayFile->Read( &nextStep, sizeof(int) );

  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool ReplayStorage2::GetHeader(NCore::MapStartInfo * info,
                               int * clientId, int * stepLength,
                               NCore::ClientSettings * clientSettings,
                               lobby::SGameParameters* gameParams )
{
  if (!mapCollection )
  {
    NI_ALWAYS_ASSERT("No map collection!");
    SetLoadingStatus(Game::EReplayStatus::Failure);
    return false;
  }

  int marker;
  replayFile->Seek( 0, SEEKORIGIN_BEGIN );
  replayFile->Read( &marker, sizeof(int) );
  if (marker != 'PRWP')
  {
    SetLoadingStatus(Game::EReplayStatus::WrongFormat);
    return false;
  }

  CObj<IBinSaver> pLoader = CreateChunklessSaver( replayFile, 0, true );

  Login::ClientVersion currentClientVersion(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_REVISION);
  Login::ClientVersion replayClientVersion;
  pLoader->Add( 1, &replayClientVersion );
  if (currentClientVersion != replayClientVersion && NGlobal::GetVar( "ignore_replay_version", 0 ).GetInt64() == 0)
  {
    NI_ALWAYS_ASSERT(NStr::StrFmt("Wrong replay client version (%d.%d.%d.%d)", replayClientVersion.major_,
                        replayClientVersion.minor_, replayClientVersion.patch_, replayClientVersion.revision_));
    SetLoadingStatus(Game::EReplayStatus::WrongVersion);
    return false;
  }

  lobby::TGameLineUp gameLineUp;
  vector<Peered::ClientInfo> clientInfos;

  pLoader->Add( 2, clientId );
  pLoader->Add( 3, &gameLineUp );
  pLoader->Add( 4, gameParams );
  pLoader->Add( 5, stepLength );
  pLoader->Add( 6, clientSettings );
  pLoader->Add( 7, &clientInfos );

  dataStart = replayFile->GetPosition(); // the step records follow the header

  StrongMT<NWorld::IMapLoader> mapLoader = mapCollection->CreateMapLoader( gameParams->mapId.c_str() );
  if ( !mapLoader )
  {
    // CreateMapLoader will assert on error, so just set status and return here
    SetLoadingStatus(Game::EReplayStatus::Failure);
    return false;
  }

  mapLoader->FillMapStartInfo( *info, gameLineUp, *gameParams );
  mapLoader->FillPlayersInfo( *info, clientInfos );

  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int ReplayStorage2::ScanLength()
{
  if (!IsOk() || dataStart < 0)
    return 0;

  const int savedPosition = replayFile->GetPosition();
  const int size = replayFile->GetSize();

  replayFile->Seek( dataStart, SEEKORIGIN_BEGIN );

  int first = NCore::INVALID_STEP;
  int last = NCore::INVALID_STEP;
  if (replayFile->Read( &first, sizeof(int) ) == sizeof(int))
  {
    last = first;

    // Record: int step, ushort commands, ushort statuses, then size-prefixed blocks.
    // A replay of a crashed game may end in the middle of a record - stop there.
    while (replayFile->GetPosition() + (int)(sizeof(int) + 2 * sizeof(ushort)) <= size)
    {
      int step = 0;
      ushort counts[2] = { 0, 0 };
      replayFile->Read( &step, sizeof(int) );
      replayFile->Read( counts, sizeof(counts) );

      bool complete = true;
      for (int i = 0; i < counts[0] + counts[1] && complete; ++i)
      {
        ushort blockSize = 0;
        if (replayFile->Read( &blockSize, sizeof(ushort) ) != sizeof(ushort) || replayFile->GetPosition() + blockSize > size)
          complete = false;
        else
          replayFile->Seek( blockSize, SEEKORIGIN_OFFSET );
      }

      if (!complete || step < last)
        break;

      last = step;
    }
  }

  replayFile->Seek( savedPosition, SEEKORIGIN_BEGIN );

  return (first == NCore::INVALID_STEP) ? 0 : Max( 0, last - first );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ReplayStorage2::ReadPosition ReplayStorage2::GetReadPosition() const
{
  ReadPosition position;
  position.filePosition = replayFile ? replayFile->GetPosition() : 0;
  position.startStep = startStep;
  position.currentStep = currentStep;
  position.nextStep = nextStep;
  position.isFinished = isFinished;
  return position;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayStorage2::SetReadPosition( const ReadPosition & position )
{
  if (!replayFile)
    return;

  replayFile->Seek( position.filePosition, SEEKORIGIN_BEGIN );
  startStep = position.startStep;
  currentStep = position.currentStep;
  nextStep = position.nextStep;
  isFinished = position.isFinished;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayStorage2::SetLoadingStatus(Game::EReplayStatus::Enum status)
{
  if (loadingStatusHandler)
    loadingStatusHandler->OnReplayStatus(status);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ReplayTransceiver::ReplayTransceiver()
: time(0.0f)
, useServerReplay(false)
, stepLength(DEFAULT_GAME_STEP_LENGTH)
, stepLengthInSeconds(DEFAULT_GAME_STEP_LENGTH / 1000.0f)
, replayMsgTimer(0.0f)
, isPaused(false)
, totalSteps(0)
, seekTarget(-1)
, replayEnded(false)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ReplayTransceiver::ReplayTransceiver( NCore::IReplayStorage* _replay, int _stepLength /*= DEFAULT_GAME_STEP_LENGTH*/ )
: replay(_replay)
, time(0.0f)
, useServerReplay(false)
, stepLength(_stepLength)
, replayMsgTimer(0.0f)
, isPaused(false)
, totalSteps(0)
, seekTarget(-1)
, replayEnded(false)
{
  NI_SYNC_FPU_START;
  stepLengthInSeconds = _stepLength/1000.0f;
  NI_SYNC_FPU_END;

  if (ReplayStorage2 * storage = Storage())
    totalSteps = storage->ScanLength();

  NGlobal::RegisterContextCmd( "timescale", this, &ReplayTransceiver::SetTimeScale );

  NGlobal::RegisterContextCmd( "replay_speed_inc", this, &ReplayTransceiver::ReplaySpeedInc );
  NGlobal::RegisterContextCmd( "replay_speed_dec", this, &ReplayTransceiver::ReplaySpeedDec );
  NGlobal::RegisterContextCmd( "replay_speed_rst", this, &ReplayTransceiver::ReplaySpeedRst );

  NGlobal::RegisterContextCmd( "replay_pause", this, &ReplayTransceiver::ReplayPause );
  NGlobal::RegisterContextCmd( "replay_seek", this, &ReplayTransceiver::ReplaySeek );
  NGlobal::RegisterContextCmd( "replay_seek_to", this, &ReplayTransceiver::ReplaySeekTo );

  NGlobal::ExecuteConfig( "replay.cfg",	NProfile::FOLDER_GLOBAL );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ReplayTransceiver::~ReplayTransceiver()
{
  snapshotThread = 0; // waits for a compression in progress
  snapshotJob = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayTransceiver::OnDestroyContent()
{
  NGlobal::UnregisterCmd( "timescale" );
  NGlobal::UnregisterCmd( "replay_speed_inc" );
  NGlobal::UnregisterCmd( "replay_speed_dec" );
  NGlobal::UnregisterCmd( "replay_speed_rst" );
  NGlobal::UnregisterCmd( "replay_seek" );
  NGlobal::UnregisterCmd( "replay_seek_to" );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ReplayStorage2 * ReplayTransceiver::Storage() const
{
  return dynamic_cast<ReplayStorage2 *>( replay.GetPtr() );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int ReplayTransceiver::GetPlayedSteps() const
{
  if (ReplayStorage2 * storage = Storage())
    return storage->GetPlayedSteps();
  return GetWorldStep();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayTransceiver::Step( float dt )
{
  NI_PROFILE_FUNCTION;

  StepReplayMessage( dt );
  PollSnapshot( false );

  if ( !world )
    return;

  // The start of the game: the first snapshot, so that any point can be reached back
  if ( snapshots.empty() && !snapshotThread )
    StartSnapshot( true );

  if ( seekTarget >= 0 )
  {
    StepSeek();
    UpdateControls();
    return;
  }

  if ( isPaused || replayEnded )
  {
    time = 0.0f;
    UpdateControls();
    return;
  }

  time += dt;

  // Several steps per frame at a high speed or a low frame rate; a very slow frame doesn't pile up a backlog
  for ( int steps = 0; time >= stepLength && steps < MAX_STEPS_PER_FRAME; ++steps )
  {
    time -= stepLength;
    if ( !StepOnce() )
      break;
  }

  if ( time > stepLength )
    time = (float)stepLength;

  UpdateControls();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool ReplayTransceiver::StepOnce()
{
  bool ignoreStep = false;

  do
  {
    int step = NCore::INVALID_STEP;
    bool hasSegment = false;

    if (useServerReplay)
    {
      NCore::SyncSegment segment;
      hasSegment = replay->GetNextSegment( segment );
      if (hasSegment)
      {
        ignoreStep = g_skipAllUntilReplayStep != NCore::INVALID_STEP && (int)segment.step < g_skipAllUntilReplayStep;
        if (!ignoreStep)
        {
          world->ExecuteCommands(segment.commands);
          world->UpdatePlayerStatuses(segment.statuses);
          step = segment.step;
        }
      }
    }
    else
    {
      NCore::ReplaySegment segment;
      hasSegment = replay->GetNextSegment( segment );
      if (hasSegment)
        world->ExecuteCommands(segment.seg);
    }

    if (!hasSegment)
    {
      seekTarget = -1;
      if (!replayEnded)
      {
        replayEnded = true;
        NI_ASSERT(IsValid(adventureScreen), "SetAdventureScreenInterface wasn't called");
        if (adventureScreen)
          adventureScreen->OnReplayEnded();
      }
      return false;
    }

    if (!ignoreStep)
    {
      world->Step( stepLengthInSeconds, stepLengthInSeconds );
      if (g_saveWorldForReplayStep > 0 && step == g_saveWorldForReplayStep)
        world->Save();
    }
  } while (ignoreStep);

  StartSnapshot( false );
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Runs steps until the seek target, at most g_replaySeekFrameBudget seconds per frame: the game keeps
// drawing and the panel shows the progress instead of freezing on a long seek.
void ReplayTransceiver::StepSeek()
{
  const NHPTimer::FTime started = NHPTimer::GetScalarTime();

  while ( seekTarget >= 0 && GetPlayedSteps() < seekTarget )
  {
    if ( !StepOnce() )
      break;

    if ( NHPTimer::GetScalarTime() - started > g_replaySeekFrameBudget )
      return;
  }

  seekTarget = -1;
  time = 0.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayTransceiver::SeekBy( float seconds )
{
  const int from = seekTarget >= 0 ? seekTarget : GetPlayedSteps(); // repeated key presses add up
  SeekToStep( from + (int)( seconds * 1000.0f / stepLength ) );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayTransceiver::SeekTo( float fraction )
{
  if ( totalSteps <= 0 )
    return;
  SeekToStep( (int)( Clamp( fraction, 0.0f, 1.0f ) * totalSteps ) );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayTransceiver::SeekToStep( int target )
{
  if ( !world )
    return;

  target = Max( target, 0 );
  if ( totalSteps > 0 )
    target = Min( target, totalSteps );

  const int current = GetPlayedSteps();

  // The latest snapshot not after the target: needed to go back, and saves time going forward
  // over a part that was already played (after a rewind)
  int best = -1;
  for ( int i = 0; i < snapshots.size(); ++i )
    if ( snapshots[i].step <= target && ( best < 0 || snapshots[i].step > snapshots[best].step ) )
      best = i;

  const int jumpThreshold = (int)( g_replaySnapshotInterval * 1000.0f / stepLength ) / 2;
  if ( target < current || ( best >= 0 && snapshots[best].step > current + jumpThreshold ) )
  {
    if ( best < 0 || !RestoreSnapshot( snapshots[best] ) )
    {
      seekTarget = -1;
      return;
    }
  }

  seekTarget = target;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayTransceiver::StartSnapshot( bool force )
{
  if ( snapshotThread || g_replaySnapshotInterval <= 0.0f )
    return;

  ReplayStorage2 * storage = Storage();
  PFWorld * pfWorld = dynamic_cast<PFWorld *>( world.GetPtr() );
  if ( !storage || !pfWorld )
    return;

  const int step = GetPlayedSteps();
  if ( !force )
  {
    const int interval = Max( 1, (int)( g_replaySnapshotInterval * 1000.0f / stepLength ) );
    for ( int i = 0; i < snapshots.size(); ++i )
      if ( abs( snapshots[i].step - step ) < interval )
        return;
  }

  // Serializing is done here (the world must not change meanwhile), compression - on a thread
  snapshotJob = new SaveCompressThreadJob( false );
  pfWorld->GetWorldChecker()->SaveWithoutCompression( *snapshotJob->GetSourceStream() );

  pendingSnapshot.step = step;
  pendingSnapshot.position = storage->GetReadPosition();
  pendingSnapshot.data = 0;

  snapshotThread = new threading::JobThread( snapshotJob, "ReplaySnapshot", 30000 );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayTransceiver::PollSnapshot( bool wait )
{
  if ( !snapshotThread || !snapshotThread->Wait( wait ? 30000 : 0 ) )
    return;

  snapshotThread = 0;

  Stream * compressed = snapshotJob->GetDestStream();
  const int size = compressed ? compressed->GetPosition() : 0;
  if ( size > 0 )
  {
    // Exact-size copy; the position stays at the end, as WorldChecker::Load expects
    CObj<MemoryStream> data = new MemoryStream( size );
    data->Write( compressed->GetBuffer(), size );
    pendingSnapshot.data = data.GetPtr();

    int index = 0;
    while ( index < snapshots.size() && snapshots[index].step < pendingSnapshot.step )
      ++index;
    if ( index < snapshots.size() && snapshots[index].step == pendingSnapshot.step )
      snapshots[index] = pendingSnapshot;
    else
      snapshots.insert( snapshots.begin() + index, pendingSnapshot );

    TrimSnapshots();
  }

  pendingSnapshot.data = 0;
  snapshotJob = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Over the memory limit every second snapshot goes (the first one, the start of the game, stays);
// the rewind then runs a bit longer from a farther snapshot.
void ReplayTransceiver::TrimSnapshots()
{
  const int limit = Max( 16, g_replaySnapshotMemoryMb ) * 1024 * 1024;

  while ( snapshots.size() > 2 )
  {
    int total = 0;
    for ( int i = 0; i < snapshots.size(); ++i )
      total += snapshots[i].data ? snapshots[i].data->GetSize() : 0;
    if ( total <= limit )
      break;

    for ( int i = snapshots.size() - 1; i >= 1; i -= 2 )
      snapshots.erase( snapshots.begin() + i );
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The same world reload as the reconnect (GameClientPW::PollWorldData)
bool ReplayTransceiver::RestoreSnapshot( const Snapshot & _snapshot )
{
  Snapshot snapshot = _snapshot;
  ReplayStorage2 * storage = Storage();
  PFWorld * pfWorld = dynamic_cast<PFWorld *>( world.GetPtr() );
  if ( !storage || !pfWorld || !snapshot.data )
    return false;

  MessageTrace( "replay: restoring the world of step %d (from step %d)", snapshot.step, GetPlayedSteps() );

  snapshot.data->Seek( snapshot.data->GetSize(), SEEKORIGIN_BEGIN );

  pfWorld->GetAIWorld()->ClearVoxelMap();
  pfWorld->GetWorldChecker()->Load( *snapshot.data );
  pfWorld->Reset();
  if ( pfWorld->GetAIContainer() )
    pfWorld->GetAIContainer()->OnReconnect();

  storage->SetReadPosition( snapshot.position );

  replayEnded = false;
  time = 0.0f;
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayTransceiver::UpdateControls()
{
  if ( adventureScreen )
    adventureScreen->UpdateReplayControls( GetPlayedSteps(), totalSteps, stepLength, NMainLoop::GetTimeScale(), isPaused, seekTarget, replayEnded );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int ReplayTransceiver::GetWorldStep() const
{
  return IsValid(world) ? world->GetStepNumber() : 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayTransceiver::SetWorld( NCore::IWorldBase * _world )
{
  world = _world;
  ptrHolder = world->GetPointerSerialization();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayTransceiver::SetTimeScale(float timeScale)
{
  NMainLoop::SetTimeScale( timeScale );

  adventureScreen->ShowReplaySpeed(timeScale);
  replayMsgTimer = g_replayMsgTime;
  MessageTrace("timescale: %f", timeScale);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool ReplayTransceiver::SetTimeScale( const char *name, const vector<wstring> &args )
{
  if ( args.size() != 1 )
  {
    systemLog( NLogg::LEVEL_MESSAGE ) << "usage: " << name << " scale" << endl;
    return true;
  }

  string arg = NStr::ToMBCS( args[0] );
  float scale = NStr::ReadFloat( arg.c_str(), arg.size() );
  if ( scale < 0.01f )
  {
    systemLog( NLogg::LEVEL_MESSAGE ) << "timescale: " << arg << " is not valid value" << endl;
    return true;
  }

  SetTimeScale( scale );
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool ReplayTransceiver::ReplaySpeedInc( const char *name, const vector<wstring> &args )
{
  float timeScale = NMainLoop::GetTimeScale();
  timeScale *= g_replaySpeedIncStep;
  if (timeScale <= g_replaySpeedIncLimit)
    SetTimeScale( timeScale );
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool ReplayTransceiver::ReplaySpeedDec( const char *name, const vector<wstring> &args )
{
  float timeScale = NMainLoop::GetTimeScale();
  timeScale *= g_replaySpeedDecStep;
  if (timeScale >= g_replaySpeedDecLimit)
    SetTimeScale( timeScale );
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool ReplayTransceiver::ReplaySpeedRst( const char *name, const vector<wstring> &args )
{
  float timeScale = NMainLoop::GetTimeScale();
  if (timeScale != 1.0f)
    SetTimeScale( 1.0f );
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool ReplayTransceiver::ReplayPause( const char *name, const vector<wstring> &args )
{
  isPaused = !isPaused;

  if (isPaused)
  {
    adventureScreen->ProcessGamePause( NGameX::PAUSE_HARD, 0 );
    adventureScreen->ShowReplayPause();
    replayMsgTimer = g_replayMsgTime;
    MessageTrace("Pause: On");
  }
  else
  {
    adventureScreen->ProcessGamePause( NGameX::PAUSE_OFF, 0 );
    MessageTrace("Pause: Off");
  }

  UpdateControls();
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// replay_seek <seconds>: negative - back; without an argument - forward by replay_seek_step seconds
bool ReplayTransceiver::ReplaySeek( const char *name, const vector<wstring> &args )
{
  float seconds = g_replaySeekStepSeconds;
  if ( !args.empty() )
  {
    const string arg = NStr::ToMBCS( args[0] );
    seconds = NStr::ToFloat( arg );
  }

  SeekBy( seconds );
  UpdateControls();
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// replay_seek_to <0..1>: a point of the timeline
bool ReplayTransceiver::ReplaySeekTo( const char *name, const vector<wstring> &args )
{
  if ( args.empty() )
  {
    systemLog( NLogg::LEVEL_MESSAGE ) << "usage: " << name << " <0..1>" << endl;
    return true;
  }

  const string arg = NStr::ToMBCS( args[0] );
  SeekTo( NStr::ToFloat( arg ) );
  UpdateControls();
  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayTransceiver::StepReplayMessage( float dt )
{
  if (replayMsgTimer > 0.0f)
  {
    replayMsgTimer -= dt / NMainLoop::GetTimeScale();
    if (replayMsgTimer <= 0.0f)
    {
      adventureScreen->HideReplayMsg();
    }
  }
}

} //namespace NWorld


REGISTER_DEV_VAR( "save_world_for_replay_step", g_saveWorldForReplayStep, STORAGE_NONE );
REGISTER_DEV_VAR( "skip_all_until_replay_step", g_skipAllUntilReplayStep, STORAGE_NONE );

REGISTER_VAR( "replay_speed_inc_limit", g_replaySpeedIncLimit, STORAGE_NONE );
REGISTER_VAR( "replay_speed_dec_limit", g_replaySpeedDecLimit, STORAGE_NONE );
REGISTER_VAR( "replay_seek_step", g_replaySeekStepSeconds, STORAGE_NONE );
REGISTER_VAR( "replay_snapshot_interval", g_replaySnapshotInterval, STORAGE_NONE );
REGISTER_VAR( "replay_snapshot_memory_mb", g_replaySnapshotMemoryMb, STORAGE_NONE );
REGISTER_VAR( "replay_seek_frame_budget", g_replaySeekFrameBudget, STORAGE_NONE );
