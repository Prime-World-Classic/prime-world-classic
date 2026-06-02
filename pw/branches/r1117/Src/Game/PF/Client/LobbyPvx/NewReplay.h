#pragma once
#include "PW_Client/GameStatistics.h"
#include "PF_GameLogic/ReplayInfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string>

namespace Login
{
  struct ClientVersion;
}

namespace Peered
{
  struct BlockHistory;
  struct BriefClientInfo;
  struct ClientInfo;
  typedef long long TSessionId;
}

namespace lobby
{
  struct SGameParameters;
  struct SGameMember;
  typedef vector<SGameMember>  TGameLineUp;
}

namespace rpc
{
  struct MemoryBlock;
}

namespace Transport
{
  typedef int TClientId;
}

struct IBinSaver;
class MemoryStream;
class FileWriteAsynchronousStream;


namespace NCore
{
struct ClientSettings;
struct MapStartInfo;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TODO: This class should be united with server's replay writer NUM_TASK
class ReplayWriter : public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_1( ReplayWriter, BaseObjectMT );
public:

  ReplayWriter();
#if defined(PW_LINUX_DB_BOOTSTRAP)
  ~ReplayWriter();
#endif

  // Methods below appear in a right order of replay format writing

  void WriteVersion(const Login::ClientVersion & _clientVersion);
  void WriteLobbyData(Transport::TClientId clientId, const lobby::TGameLineUp & _gameLineUp, const lobby::SGameParameters & _gameParams);
  void WriteGSData(int stepLength, const ClientSettings & clientSettings, const vector<Peered::ClientInfo> & clientInfos);
  void WriteStartGame(Peered::TSessionId serverId, int step);
  void WriteStepData(int step, const nstl::vector<rpc::MemoryBlock> & commands, const vector<Peered::BriefClientInfo> & statuses);
  void WriteStartGameInfo(const NGameX::ReplayInfo & _replayInfo);
  void WriteFinishGame(int step, const StatisticService::RPC::SessionClientResults & _sessionResults, const NGameX::ReplayInfo & _replayInfo);
  void WriteSessionInfoToFile(const StatisticService::RPC::SessionClientResults & _sessionResults, const NGameX::ReplayInfo & _replayInfo);
#if defined(PW_LINUX_DB_BOOTSTRAP)
  bool IsLinuxReplayOpen() const { return linuxReplayFile != 0; }
  const string& GetLinuxReplayFilePath() const { return replayFilePath; }
  size_t GetLinuxReplayStartWrites() const { return linuxReplayStartWrites; }
  size_t GetLinuxReplayStepWrites() const { return linuxReplayStepWrites; }
  size_t GetLinuxReplayCommandWrites() const { return linuxReplayCommandWrites; }
  size_t GetLinuxReplayStatusWrites() const { return linuxReplayStatusWrites; }
  size_t GetLinuxReplayBytesWritten() const { return linuxReplayBytesWritten; }
  size_t GetLinuxReplayWriteFailures() const { return linuxReplayWriteFailures; }
#endif

private:
  bool versionWritten;
  bool lobbyDataWritten;
  bool gsDataWritten;
  bool headerWritten;
  bool infoheaderWritten;

  CObj<IBinSaver> saver;
  CObj<MemoryStream> header;
  CObj<FileWriteAsynchronousStream> replayFile;
  string informationFileName;
  string replaysFolderPath;
  string replayFilePath;
  std::string separator;
#if defined(PW_LINUX_DB_BOOTSTRAP)
  void LinuxEnsureReplayFile();
  void LinuxWriteReplay(const void* data, size_t size);

  FILE* linuxReplayFile;
  size_t linuxReplayStartWrites;
  size_t linuxReplayStepWrites;
  size_t linuxReplayCommandWrites;
  size_t linuxReplayStatusWrites;
  size_t linuxReplayBytesWritten;
  size_t linuxReplayWriteFailures;
#endif
};

} // namespace NCore
