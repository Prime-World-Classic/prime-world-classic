#include "stdafx.h"
#include "ReplayTransceiver.h"

#if defined(PW_LINUX_DB_BOOTSTRAP)

#include "Core/CommandSerializer.h"
#include "Core/GameCommand.h"
#include "Core/WorldBase.h"
#include "HybridServer/PeeredTypes.h"
#include "System/MemoryStream.h"
#include "System/StrProc.h"

#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>

namespace
{
static const char kLinuxReplayMagic[] = "PWLXREPLAY1\n";
static const char kLinuxReplayInfoMagic[] = "PWLXREPLAYINFO1";

bool ReadLinuxReplayBytes(std::ifstream* input, void* data, size_t size, size_t* bytesRead)
{
  if (!input || !data || size == 0)
    return false;

  input->read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
  if (input->gcount() != static_cast<std::streamsize>(size))
    return false;

  if (bytesRead)
    *bytesRead += size;
  return true;
}

int ParseLinuxReplayInt(const std::string& value, int fallback)
{
  if (value.empty())
    return fallback;
  return atoi(value.c_str());
}

std::vector<std::string> SplitLinuxReplayLine(const std::string& line)
{
  std::vector<std::string> parts;
  size_t start = 0;
  for (;;)
  {
    const size_t tab = line.find('\t', start);
    if (tab == std::string::npos)
    {
      parts.push_back(line.substr(start));
      break;
    }

    parts.push_back(line.substr(start, tab - start));
    start = tab + 1;
  }
  return parts;
}
}

namespace NWorld
{

ReplayStorage2::ReplayStorage2()
  : linuxReplayCursor(0),
    linuxSyncCursor(0),
    linuxOk(false),
    linuxStartStep(-1),
    linuxFirstStep(-1),
    linuxLastStep(-1),
    linuxLoadedCommands(0),
    linuxLoadedStatuses(0),
    linuxDecodeFailures(0),
    linuxHeaderReady(false),
    linuxHeaderClientId(-1),
    linuxHeaderStepLength(DEFAULT_GAME_STEP_LENGTH),
    linuxError("inactive")
{
}

ReplayStorage2::ReplayStorage2( NCore::ReplayBufferMode mode, const char * fileName, NWorld::IMapCollection *, NGameX::LoadingStatusHandler * )
  : linuxReplayCursor(0),
    linuxSyncCursor(0),
    linuxOk(false),
    linuxStartStep(-1),
    linuxFirstStep(-1),
    linuxLastStep(-1),
    linuxLoadedCommands(0),
    linuxLoadedStatuses(0),
    linuxDecodeFailures(0),
    linuxHeaderReady(false),
    linuxHeaderClientId(-1),
    linuxHeaderStepLength(DEFAULT_GAME_STEP_LENGTH),
    linuxError("inactive")
{
  if (mode != NCore::REPLAY_BUFFER_READ)
  {
    LinuxFail("write-unsupported");
    return;
  }

  LinuxLoad(fileName);
  LinuxLoadHeader(fileName);
}

bool ReplayStorage2::LinuxFail(const char* message)
{
  linuxOk = false;
  linuxError = message ? message : "failed";
  return false;
}

bool ReplayStorage2::LinuxLoad(const char* fileName)
{
  if (!fileName || !fileName[0])
    return LinuxFail("inactive");

  std::ifstream input(fileName, std::ios::binary);
  if (!input)
    return LinuxFail("open-failed");

  input.seekg(0, std::ios::end);
  const std::streamoff fileSizeStream = input.tellg();
  if (fileSizeStream <= 0)
    return LinuxFail("empty");

  const size_t fileSize = static_cast<size_t>(fileSizeStream);
  size_t bytesRead = 0;
  input.seekg(0, std::ios::beg);

  char magic[sizeof(kLinuxReplayMagic) - 1] = {0};
  if (!ReadLinuxReplayBytes(&input, magic, sizeof(magic), &bytesRead) ||
      memcmp(magic, kLinuxReplayMagic, sizeof(magic)) != 0)
    return LinuxFail("bad-magic");

  long long serverId = 0;
  (void)serverId;
  if (!ReadLinuxReplayBytes(&input, &serverId, sizeof(serverId), &bytesRead) ||
      !ReadLinuxReplayBytes(&input, &linuxStartStep, sizeof(linuxStartStep), &bytesRead))
    return LinuxFail("truncated-start");

  while (bytesRead < fileSize)
  {
    if (fileSize - bytesRead < sizeof(int) + sizeof(unsigned short) + sizeof(unsigned short))
      return LinuxFail("truncated-record-header");

    int step = -1;
    unsigned short commandsCount = 0;
    unsigned short statusesCount = 0;
    if (!ReadLinuxReplayBytes(&input, &step, sizeof(step), &bytesRead) ||
        !ReadLinuxReplayBytes(&input, &commandsCount, sizeof(commandsCount), &bytesRead) ||
        !ReadLinuxReplayBytes(&input, &statusesCount, sizeof(statusesCount), &bytesRead))
      return LinuxFail("truncated-record");

    NCore::ReplaySegment segment;
    segment.deltaTime = 0.0f;
    segment.crc = 0;
    segment.seg.reserve(commandsCount);
    NCore::SyncSegment::TStatuses statuses;
    statuses.reserve(statusesCount);

    for (unsigned int i = 0; i < commandsCount; ++i)
    {
      unsigned short commandSize = 0;
      if (fileSize - bytesRead < sizeof(commandSize) ||
          !ReadLinuxReplayBytes(&input, &commandSize, sizeof(commandSize), &bytesRead))
        return LinuxFail("truncated-command-size");

      if (commandSize == 0)
        return LinuxFail("empty-command");

      if (fileSize - bytesRead < commandSize)
        return LinuxFail("truncated-command");

      std::vector<char> commandBytes(commandSize);
      if (!ReadLinuxReplayBytes(&input, &commandBytes[0], commandSize, &bytesRead))
        return LinuxFail("truncated-command");

      MemoryStream commandStream(static_cast<int>(commandBytes.size()));
      commandStream.Write(&commandBytes[0], commandSize);
      commandStream.Seek(0, SEEKORIGIN_BEGIN);
      CObj<CObjectBase> commandObject = NCore::ReadCommandFromStream(
        static_cast<Stream*>(&commandStream),
        0);
      CDynamicCast<NCore::PackedWorldCommand> packedCommand(commandObject);
      if (!packedCommand)
      {
        ++linuxDecodeFailures;
        return LinuxFail("decode-failed");
      }

      segment.seg.push_back(packedCommand.GetPtr());
      ++linuxLoadedCommands;
    }

    for (unsigned int i = 0; i < statusesCount; ++i)
    {
      unsigned short statusSize = 0;
      if (fileSize - bytesRead < sizeof(statusSize) ||
          !ReadLinuxReplayBytes(&input, &statusSize, sizeof(statusSize), &bytesRead))
        return LinuxFail("truncated-status-size");

      if (fileSize - bytesRead < statusSize)
        return LinuxFail("truncated-status");

      if (statusSize != sizeof(Peered::BriefClientInfo))
        return LinuxFail("bad-status-size");

      Peered::BriefClientInfo briefStatus;
      if (!ReadLinuxReplayBytes(&input, &briefStatus, sizeof(briefStatus), &bytesRead))
        return LinuxFail("truncated-status");

      NCore::ClientStatus status;
      status.clientId = briefStatus.clientId;
      status.status = static_cast<int>(briefStatus.status);
      status.step = briefStatus.step - linuxStartStep;
      statuses.push_back(status);
      ++linuxLoadedStatuses;
    }

    if (linuxFirstStep < 0)
      linuxFirstStep = step;
    linuxLastStep = step;
    linuxSegmentSteps.push_back(step);
    linuxSegmentStatuses.push_back(statuses);
    linuxSegments.push_back(segment);
  }

  if (bytesRead != fileSize)
    return LinuxFail("bounds-mismatch");

  linuxOk = true;
  linuxError = "none";
  return true;
}

void ReplayStorage2::LinuxLoadHeader(const char* fileName)
{
  linuxHeaderReady = false;
  linuxHeaderMapStartInfo = NCore::MapStartInfo();
  linuxHeaderClientSettings = NCore::ClientSettings();
  linuxHeaderClientId = -1;
  linuxHeaderStepLength = DEFAULT_GAME_STEP_LENGTH;

  if (!fileName || !fileName[0])
    return;

  const std::string infoFileName = std::string(fileName) + ".info";
  std::ifstream input(infoFileName.c_str());
  if (!input)
    return;

  std::string line;
  if (!std::getline(input, line) || line != kLinuxReplayInfoMagic)
    return;

  size_t expectedPlayers = 0;
  while (std::getline(input, line))
  {
    std::vector<std::string> parts = SplitLinuxReplayLine(line);
    if (parts.empty())
      continue;

    const std::string& key = parts[0];
    const std::string value = parts.size() > 1 ? parts[1] : std::string();
    if (key == "clientId")
      linuxHeaderClientId = ParseLinuxReplayInt(value, -1);
    else if (key == "stepLength")
      linuxHeaderStepLength = ParseLinuxReplayInt(value, DEFAULT_GAME_STEP_LENGTH);
    else if (key == "mapDescName")
      linuxHeaderMapStartInfo.mapDescName = value.c_str();
    else if (key == "replayName")
      linuxHeaderMapStartInfo.replayName = value.c_str();
    else if (key == "randomSeed")
      linuxHeaderMapStartInfo.randomSeed = ParseLinuxReplayInt(value, 0);
    else if (key == "isCustomGame")
      linuxHeaderMapStartInfo.isCustomGame = ParseLinuxReplayInt(value, 0) != 0;
    else if (key == "minigameEnabled")
      linuxHeaderClientSettings.minigameEnabled = ParseLinuxReplayInt(value, 0) != 0;
    else if (key == "logicParam1")
      linuxHeaderClientSettings.logicParam1 = static_cast<float>(atof(value.c_str()));
    else if (key == "aiForLeaversEnabled")
      linuxHeaderClientSettings.aiForLeaversEnabled = ParseLinuxReplayInt(value, 0) != 0;
    else if (key == "aiForLeaversThreshold")
      linuxHeaderClientSettings.aiForLeaversThreshold = ParseLinuxReplayInt(value, 0);
    else if (key == "playerCount")
      expectedPlayers = static_cast<size_t>(ParseLinuxReplayInt(value, 0));
    else if (key == "player" && parts.size() >= 8)
    {
      NCore::PlayerStartInfo player;
      player.playerID = ParseLinuxReplayInt(parts[1], 0);
      player.teamID = static_cast<NCore::ETeam::Enum>(ParseLinuxReplayInt(parts[2], static_cast<int>(NCore::ETeam::None)));
      player.originalTeamID = static_cast<NCore::ETeam::Enum>(ParseLinuxReplayInt(parts[3], static_cast<int>(NCore::ETeam::None)));
      player.playerType = static_cast<NCore::EPlayerType::Enum>(ParseLinuxReplayInt(parts[4], static_cast<int>(NCore::EPlayerType::Invalid)));
      player.userID = ParseLinuxReplayInt(parts[5], -1);
      player.zzimaSex = static_cast<NCore::ESex::Enum>(ParseLinuxReplayInt(parts[6], static_cast<int>(NCore::ESex::Undefined)));
      player.nickname = NStr::ToUnicode(string(parts[7].c_str()));
      // Columns after nickname were added after the initial Linux replay sidecar format.
      if (parts.size() > 8)
        player.playerInfo.heroId = static_cast<uint>(ParseLinuxReplayInt(parts[8], 0));
      if (parts.size() > 9)
        player.playerInfo.heroSkin = parts[9].c_str();
      if (parts.size() > 10)
        player.playerInfo.locale = parts[10].c_str();
      if (parts.size() > 11)
        player.playerInfo.heroLevel = static_cast<uint>(ParseLinuxReplayInt(parts[11], 0));
      if (parts.size() > 12)
        player.playerInfo.heroExp = ParseLinuxReplayInt(parts[12], 0);
      if (parts.size() > 13)
        player.playerInfo.heroRating = static_cast<float>(atof(parts[13].c_str()));
      if (parts.size() > 14)
        player.playerInfo.hasPremium = ParseLinuxReplayInt(parts[14], 0) != 0;
      if (parts.size() > 15)
        player.playerInfo.basket = static_cast<NCore::EBasket::Enum>(ParseLinuxReplayInt(parts[15], static_cast<int>(NCore::EBasket::Undefined)));
      if (parts.size() > 16)
        player.playerInfo.isAnimatedAvatar = ParseLinuxReplayInt(parts[16], 1) != 0;
      if (parts.size() > 17)
        player.playerInfo.partyId = static_cast<uint>(ParseLinuxReplayInt(parts[17], 0));
      linuxHeaderMapStartInfo.playersInfo.push_back(player);
    }
  }

  linuxHeaderReady =
    linuxHeaderStepLength > 0 &&
    !linuxHeaderMapStartInfo.mapDescName.empty() &&
    expectedPlayers == linuxHeaderMapStartInfo.playersInfo.size();
}

bool ReplayStorage2::GetNextSegment( NCore::ReplaySegment& segOut )
{
  if (linuxReplayCursor >= linuxSegments.size())
    return false;

  segOut = linuxSegments[linuxReplayCursor];
  ++linuxReplayCursor;
  return true;
}

bool ReplayStorage2::GetNextSegment( NCore::SyncSegment & segOut )
{
  if (linuxSyncCursor >= linuxSegments.size() ||
      linuxSyncCursor >= linuxSegmentSteps.size() ||
      linuxSyncCursor >= linuxSegmentStatuses.size())
    return false;

  segOut.commands = linuxSegments[linuxSyncCursor].seg;
  segOut.statuses = linuxSegmentStatuses[linuxSyncCursor];
  segOut.step = static_cast<uint>(linuxSegmentSteps[linuxSyncCursor]);
  ++linuxSyncCursor;
  return true;
}

bool ReplayStorage2::GetHeader( NCore::MapStartInfo * info, int * clientId, int * stepLength, NCore::ClientSettings * clientSettings, lobby::SGameParameters* gameParams )
{
  if (!linuxHeaderReady)
    return false;

  if (info)
    *info = linuxHeaderMapStartInfo;
  if (clientId)
    *clientId = linuxHeaderClientId;
  if (stepLength)
    *stepLength = linuxHeaderStepLength;
  if (clientSettings)
    *clientSettings = linuxHeaderClientSettings;
  if (gameParams)
  {
    gameParams->mapId = linuxHeaderMapStartInfo.mapDescName;
    gameParams->randomSeed = linuxHeaderMapStartInfo.randomSeed;
    gameParams->slotsCount = static_cast<int>(linuxHeaderMapStartInfo.playersInfo.size());
    gameParams->customGame = linuxHeaderMapStartInfo.isCustomGame;
  }
  return true;
}

void ReplayStorage2::SetLoadingStatus( Game::EReplayStatus::Enum )
{
}

ReplayTransceiver::ReplayTransceiver( NCore::IReplayStorage* _replay, int _stepLength )
  : replay( _replay )
  , time( 0.0f )
  , useServerReplay( false )
  , stepLength( _stepLength )
  , stepLengthInSeconds( _stepLength / 1000.0f )
  , replayMsgTimer( 0.0f )
  , isPaused( false )
{
}

void ReplayTransceiver::OnDestroyContent()
{
}

void ReplayTransceiver::Step( float dt )
{
  StepReplayMessage( dt );

  if ( isPaused )
    return;

  time += dt;

  if ( time < stepLength || !world || !replay )
    return;

  time = time - stepLength;

  if ( useServerReplay )
  {
    NCore::SyncSegment segment;
    if ( replay->GetNextSegment( segment ) )
    {
      world->ExecuteCommands( segment.commands );
      world->UpdatePlayerStatuses( segment.statuses );
      world->Step( stepLengthInSeconds, stepLengthInSeconds );
    }
  }
  else
  {
    NCore::ReplaySegment segment;
    if ( replay->GetNextSegment( segment ) )
    {
      world->ExecuteCommands( segment.seg );
      world->Step( stepLengthInSeconds, stepLengthInSeconds );
    }
  }
}

int ReplayTransceiver::GetWorldStep() const
{
  return world ? world->GetStepNumber() : 0;
}

void ReplayTransceiver::SetWorld( NCore::IWorldBase * _world )
{
  world = _world;
  ptrHolder = 0;
}

void ReplayTransceiver::SetTimeScale( float )
{
}

bool ReplayTransceiver::SetTimeScale( const char *, const vector<wstring> & )
{
  return true;
}

bool ReplayTransceiver::ReplaySpeedInc( const char *, const vector<wstring> & )
{
  return true;
}

bool ReplayTransceiver::ReplaySpeedDec( const char *, const vector<wstring> & )
{
  return true;
}

bool ReplayTransceiver::ReplaySpeedRst( const char *, const vector<wstring> & )
{
  return true;
}

bool ReplayTransceiver::ReplayPause( const char *, const vector<wstring> & )
{
  return true;
}

void ReplayTransceiver::StepReplayMessage( float )
{
}

} //namespace NWorld

NI_DEFINE_REFCOUNT( NWorld::ReplayTransceiver );

#else

#include "Core/Replay.h"
#include "Core/WorldBase.h"
#include "Core/WorldCommand.h"

#include "System/ChunklessSaver.h"
#include "System/SyncProcessorState.h"
#include "System/ConfigFiles.h"

#include "PF_GameLogic/GameMaps.h"
#include "PF_GameLogic/IAdventureScreen.h"

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

static float g_replaySpeedIncLimit = 4.0f;
static float g_replaySpeedDecLimit = 0.25f;
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
void ReplayStorage2::SetLoadingStatus(Game::EReplayStatus::Enum status)
{
  if (loadingStatusHandler)
    loadingStatusHandler->OnReplayStatus(status);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ReplayTransceiver::ReplayTransceiver( NCore::IReplayStorage* _replay, int _stepLength /*= DEFAULT_GAME_STEP_LENGTH*/ )
: replay(_replay)
, time(0.0f)
, useServerReplay(false)
, stepLength(_stepLength)
, replayMsgTimer(0.0f)
, isPaused(false)
{
  NI_SYNC_FPU_START;
  stepLengthInSeconds = _stepLength/1000.0f;
  NI_SYNC_FPU_END;

  NGlobal::RegisterContextCmd( "timescale", this, &ReplayTransceiver::SetTimeScale );

  NGlobal::RegisterContextCmd( "replay_speed_inc", this, &ReplayTransceiver::ReplaySpeedInc );
  NGlobal::RegisterContextCmd( "replay_speed_dec", this, &ReplayTransceiver::ReplaySpeedDec );
  NGlobal::RegisterContextCmd( "replay_speed_rst", this, &ReplayTransceiver::ReplaySpeedRst );

  NGlobal::RegisterContextCmd( "replay_pause", this, &ReplayTransceiver::ReplayPause );

  NGlobal::ExecuteConfig( "replay.cfg",	NProfile::FOLDER_GLOBAL );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayTransceiver::OnDestroyContent()
{
  NGlobal::UnregisterCmd( "timescale" );
  NGlobal::UnregisterCmd( "replay_speed_inc" );
  NGlobal::UnregisterCmd( "replay_speed_dec" );
  NGlobal::UnregisterCmd( "replay_speed_rst" );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayTransceiver::Step( float dt )
{
  NI_PROFILE_FUNCTION;

  StepReplayMessage( dt );

  if (isPaused)
    return;

  time += dt;

  if ( time < stepLength || !world )
    return;

  time = time - stepLength;

  bool replayEnded = false;
  bool ignoreStep = false;

  int step = NCore::INVALID_STEP;

  do
  {
    if (useServerReplay)
    {
      NCore::SyncSegment segment;
      if ( replay->GetNextSegment( segment ) )
      {
        ignoreStep = g_skipAllUntilReplayStep != NCore::INVALID_STEP && (int)segment.step < g_skipAllUntilReplayStep;
        if (!ignoreStep)
        {
          world->ExecuteCommands(segment.commands);
          world->UpdatePlayerStatuses(segment.statuses);
          step = segment.step;
        }
      }
      else
        replayEnded = true;
    }
    else
    {
      NCore::ReplaySegment segment;
      if ( replay->GetNextSegment( segment ) )
        world->ExecuteCommands(segment.seg);
      else
        replayEnded = true;
    }


    if (replayEnded)
    {
      NI_ASSERT(IsValid(adventureScreen), "SetAdventureScreenInterface wasn't called");
      adventureScreen->OnReplayEnded();
    }
    else
    {
      if (!ignoreStep)
      {
        world->Step( stepLengthInSeconds, stepLengthInSeconds );
        if (g_saveWorldForReplayStep > 0 && step == g_saveWorldForReplayStep)
          world->Save();
      }
    }
  } while (ignoreStep);
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

NI_DEFINE_REFCOUNT( NWorld::ReplayTransceiver );

#endif
