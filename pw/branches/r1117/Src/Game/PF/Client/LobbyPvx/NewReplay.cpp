#include "stdafx.h"
#include <vector>
#include "NewReplay.h"
#include <iostream>
#include <fstream>
#include "base64.h"

#include <time.h>

#include <Windows.h>

#include "Network/LoginClientVersion.h"
#include "System/FileSystem/FileUtils.h"
#include "System/JobThread.h"
#include "System/SyncPrimitives.h"
#include "System/ChunklessSaver.h"
#include "System/LogFileName.h"
#include "System/ProfileManager.h"
#include "HybridServer/Peered.h"
#include "HybridServer/PeeredTypes.h"
#include "Core/GameTypes.h"
#include "PW_Client/GameStatistics.h"
#include "System/SystemLog.h"

extern string g_sessionToken; // 32 hex chars from the launcher protocol, empty for a local game

namespace
{
  // The web session token is safe to put into a file name only as 32 hex chars
  bool IsFileNameSessionToken(const string & token)
  {
    if (token.size() != 32)
      return false;

    for (int i = 0; i < token.size(); ++i)
    {
      const char c = token[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
        return false;
    }

    return true;
  }

	// Picks a free replay file name; the file itself is created by the replay writer thread.
	// A game started by the launcher is named by its web session token: after the game the
	// launcher picks the replay of exactly that session (and uploads it). A reconnect writes
	// its own file, it differs by the timestamp. A local game keeps the server session id.
	bool ChooseReplayFilePath(string &replaysFolderPath, string &replayFilePath, const Peered::TSessionId serverId)
  {
    replaysFolderPath = GetFullFolderPath(NProfile::FOLDER_REPLAYS);
    if (!NFile::DoesFolderExist(replaysFolderPath))
      NFile::CreatePath(replaysFolderPath);

    string sessionTag;
    if (IsFileNameSessionToken(g_sessionToken))
      sessionTag = g_sessionToken;
    else
      sessionTag = NStr::StrFmt("%016llx", serverId);

    string replayFileName;

    for (unsigned index = 0; index < 10; ++index)
    {
      string suffix = sessionTag;
      if (index)
        suffix = NStr::StrFmt("%s-%u", sessionTag.c_str(), index);
      replayFileName = NDebug::GenerateDebugFileName(suffix.c_str(), "pwrp", 0, false);
      replayFilePath = NFile::Combine(replaysFolderPath, replayFileName);

      if (!NFile::DoesFileExist(replayFilePath))
      {
        MessageTrace("replay_writer: recording to %s", replayFileName.c_str());
        return true;
      }
    }

    MessageTrace("replay_writer: no free replay file name for session %s", sessionTag.c_str());
    return false;
  }

  void PutBytes(vector<char> & buffer, const void * data, int size)
  {
    if (size <= 0)
      return;
    const int from = buffer.size();
    buffer.resize(from + size, 0);
    memcpy(&buffer[from], data, size);
  }
	
	// Convert an ASCII string to a Unicode String
	std::wstring utf8_decode(const std::string &str)
	{	
		std::wstring wstrTo;
		wchar_t *wszTo = new wchar_t[str.length() + 1];
		wszTo[str.size()] = L'\0';
		MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, wszTo, (int)str.length());
		wstrTo = wszTo;
		delete[] wszTo;
		return wstrTo;
	}

  // Convert an Unicode string to a ASCII String
  const std::string utf8_encode(const std::wstring &wstr)
	{
		if (wstr.empty()) return std::string();
		int sz = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), 0, 0, 0, 0);
		std::string res(sz, 0);
		WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &res[0], sz, 0, 0);
		return res;
	}
	
	// Get current date/time, format 
	const std::string currentDateTime() 
	{
		time_t     now = time(0);
		struct tm  tstruct;
		char       buf[80];
		tstruct = *localtime(&now);
		strftime(buf, sizeof(buf), "%d %m %Y %H:%M", &tstruct);

		return buf;
	}
}

namespace NCore
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Writes the replay file on its own thread. The game thread only copies bytes into the memory
// buffer (Append), so a slow disk, an antivirus or a cloud-synced Documents folder can't stall
// the game step. Data goes to the file about once a second with a regular buffered write
// (no FILE_FLAG_WRITE_THROUGH / NO_BUFFERING, no waiting for the disk on the game thread).
class ReplayFileWorker : public threading::IThreadJob, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2( ReplayFileWorker, threading::IThreadJob, BaseObjectMT );

public:
  explicit ReplayFileWorker(const string & _fileName)
  : fileName(_fileName)
  , evtData(false, false)
  , closeRequested(false)
  , overflow(false)
  , file(0)
  {
    inputBuffer.reserve(WAKE_SIZE * 2);
    workingBuffer.reserve(WAKE_SIZE * 2);
  }

  ~ReplayFileWorker()
  {
    CloseFile();
  }

  void Append(const void * data, int size)
  {
    if (size <= 0)
      return;

    bool wake = false;
    {
      threading::MutexLock guard(mutex);

      if (closeRequested || overflow)
        return;

      if (inputBuffer.size() + size > MAX_PENDING_SIZE)
      {
        // The disk doesn't keep up at all; drop the rest instead of eating memory
        overflow = true;
        return;
      }

      PutBytes(inputBuffer, data, size);
      wake = inputBuffer.size() >= WAKE_SIZE;
    }

    if (wake)
      evtData.Set();
  }

  // Writes out everything appended so far and closes the file; later Appends are ignored
  void RequestClose()
  {
    {
      threading::MutexLock guard(mutex);
      closeRequested = true;
    }
    evtData.Set();
  }

private:
  enum
  {
    FLUSH_PERIOD_MS = 1000,
    WAKE_SIZE = 64 * 1024,
    MAX_PENDING_SIZE = 64 * 1024 * 1024
  };

  virtual void Work(volatile bool & isRunning)
  {
    file = fopen(fileName.c_str(), "wb");
    if (!file)
      MessageTrace("replay_writer: failed to create %s", fileName.c_str());

    while (true)
    {
      evtData.Wait(FLUSH_PERIOD_MS);

      bool closing = false;
      {
        threading::MutexLock guard(mutex);
        closing = closeRequested || !isRunning;
        inputBuffer.swap(workingBuffer);
      }

      WriteOut();

      if (closing)
        break;
    }

    if (overflow)
      MessageTrace("replay_writer: pending data limit exceeded, replay %s is truncated", fileName.c_str());

    CloseFile();
  }

  void WriteOut()
  {
    if (workingBuffer.empty())
      return;

    if (file)
    {
      const size_t size = workingBuffer.size();
      if (fwrite(&workingBuffer[0], 1, size, file) == size)
      {
        fflush(file); // hands the data to the OS cache only, no disk sync
      }
      else
      {
        MessageTrace("replay_writer: write to %s failed, replay recording stopped", fileName.c_str());
        CloseFile();
      }
    }

    workingBuffer.clear();
  }

  void CloseFile()
  {
    if (file)
      fclose(file);
    file = 0;
  }

  const string fileName;
  threading::Mutex mutex;
  threading::Event evtData;
  vector<char> inputBuffer;
  vector<char> workingBuffer;
  bool closeRequested;
  bool overflow;
  FILE * file;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ReplayWriter::ReplayWriter()
: versionWritten(false)
, lobbyDataWritten(false)
, gsDataWritten(false)
, headerWritten(false)
, infoheaderWritten(false)
, finished(false)
, replaysFolderPath("")
, replayFilePath("")
, separator(";")
{
  header = new MemoryStream(1<<16);
  const int marker = 'PRWP';
  header->Write(&marker, sizeof(marker));
  saver = CreateChunklessSaver(header, 0, false);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ReplayWriter::~ReplayWriter()
{
  // Game left without a finish (leave, session error, reconnect): keep what was recorded
  if (fileWorker)
    fileWorker->RequestClose();

  fileThread = 0; // joins the writer thread: at most about a second of data is left to write
  fileWorker = 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayWriter::WriteVersion(const Login::ClientVersion & _clientVersion)
{
  MessageTrace( "replay_writer.WriteVersion: start");
  NI_VERIFY(header->GetPosition() == 4, "Wrong replay's header writing order", return;);

  Login::ClientVersion * clientVersion = const_cast<Login::ClientVersion *>(&_clientVersion);
  saver->Add(1, clientVersion);

  versionWritten = true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayWriter::WriteLobbyData(Transport::TClientId clientId, 
                                  const lobby::TGameLineUp & _gameLineUp, 
                                  const lobby::SGameParameters & _gameParams)
{
  NI_VERIFY(versionWritten, "Wrong replay's header writing order", return;);
  NI_VERIFY(!lobbyDataWritten, "Lobby data is already written to replay", return;);

  lobby::TGameLineUp * gameLineUp = const_cast<lobby::TGameLineUp *>(&_gameLineUp);
  lobby::SGameParameters * gameParams = const_cast<lobby::SGameParameters *>(&_gameParams);
  saver->Add(2, &clientId);
  saver->Add(3, gameLineUp);
  saver->Add(4, gameParams);

  lobbyDataWritten = true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayWriter::WriteGSData(int stepLength, 
                               const ClientSettings & _clientSettings, 
                               const vector<Peered::ClientInfo> & _clientInfos)
{
  NI_VERIFY(lobbyDataWritten, "Wrong replay's header writing order", return;);
  NI_VERIFY(!gsDataWritten, "GS data is already written to replay", return;);

  NCore::ClientSettings * clientSettings = const_cast<ClientSettings *>(&_clientSettings);
  vector<Peered::ClientInfo> * clientInfos = const_cast<vector<Peered::ClientInfo> *>(&_clientInfos);
  saver->Add(5, &stepLength);
  saver->Add(6, clientSettings);
  saver->Add(7, clientInfos);

  gsDataWritten = true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayWriter::WriteStartGame(Peered::TSessionId serverId, int step)
{
  MessageTrace( "replay_writer.WriteStartGame: start");
  NI_PROFILE_FUNCTION;

  NI_VERIFY(gsDataWritten, "Wrong replay's header writing order", return;);
  NI_VERIFY(!headerWritten, "Header is already written to replay", return;);

  if (!ChooseReplayFilePath(replaysFolderPath, replayFilePath, serverId))
    return;

  fileWorker = new ReplayFileWorker(replayFilePath);
  fileThread = new threading::JobThread(fileWorker, "ReplayWriter", 10000, 65536);

  //add replay file path to replays info (if started from castle)
  if (infoheaderWritten)
  {
    std::ofstream infoFileWriter;
    std::string infoFileNameStr(informationFileName.c_str());
    std::wstring convertedToUtf8InfoFileName = utf8_decode(infoFileNameStr);
    infoFileWriter.open(convertedToUtf8InfoFileName.c_str(), std::ios_base::app | std::ios_base::out);

    const std::string replayFilePathEnc(replayFilePath.c_str()); 
    infoFileWriter << utf8_encode(utf8_decode(replayFilePathEnc));

    infoFileWriter.close();
    MessageTrace( "replay_writer.WriteStartGameInfo: writed replayFilePath %s", replayFilePath);
  }
  ///
  
  fileWorker->Append(header->GetBuffer(), header->GetPosition());
  fileWorker->Append(&step, sizeof(step));

  headerWritten = true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayWriter::WriteStepData(int step, 
                                 const nstl::vector<rpc::MemoryBlock> & commands, 
                                 const vector<Peered::BriefClientInfo> & statuses)
{
  NI_PROFILE_FUNCTION;

  if (!fileWorker || finished) // No file (recording failed to start) or the game is already finished
    return;

  NI_VERIFY(headerWritten, "Replay header wasn't written", return;);

  // The whole step goes to the worker as one record: a single memcpy under the lock
  recordBuffer.clear();
  PutBytes(recordBuffer, &step, sizeof(step));
  unsigned short commandsCount = commands.size();
  unsigned short statusesCount = statuses.size();
  PutBytes(recordBuffer, &commandsCount, sizeof(commandsCount));
  PutBytes(recordBuffer, &statusesCount, sizeof(statusesCount));

  for (int i = 0; i < commands.size(); ++i)
  {
    const rpc::MemoryBlock & block = commands[i];
    unsigned short size = block.size;
    PutBytes(recordBuffer, &size, sizeof(size));
    PutBytes(recordBuffer, block.memory, block.size);
  }

  for (int i = 0; i < statuses.size(); ++i)
  {
    const Peered::BriefClientInfo & status = statuses[i];
    unsigned short size = sizeof(status);
    PutBytes(recordBuffer, &size, sizeof(size));
    PutBytes(recordBuffer, &status, sizeof(status));
  }

  fileWorker->Append(&recordBuffer[0], recordBuffer.size());
}


void ReplayWriter::WriteStartGameInfo(const NGameX::ReplayInfo & _replayInfo)
{
  //set replaysFolderPath If null (this case if game started from castle)
  if (!headerWritten)
  {
    replaysFolderPath = GetFullFolderPath(NProfile::FOLDER_REPLAYS);
    if (!NFile::DoesFolderExist(replaysFolderPath))
      NFile::CreatePath(replaysFolderPath);
  }

  MessageTrace( "replay_writer.WriteStartGameInfo: start");
  informationFileName = NFile::Combine(replaysFolderPath, NStr::StrFmt("replaysinfo-%d.castle", _replayInfo.auid));
  MessageTrace( "replay_writer.WriteStartGameInfo: %s", informationFileName);
  //_replayInfo.auid
  //infoFile - CheckIsExist

  const std::string infoFileNameStr(informationFileName.c_str());
  const std::wstring convertedToUtf8InfoFileName = utf8_decode(infoFileNameStr);
  std::ifstream infoFileReader(convertedToUtf8InfoFileName.c_str());
  bool isExist = false;
  if (infoFileReader.is_open())
    isExist = infoFileReader.peek() != -1;
  infoFileReader.close();

  MessageTrace( "replay_writer.WriteStartGameInfo: reader closed... start writer");
  std::ofstream infoFileWriter;
  infoFileWriter.open(convertedToUtf8InfoFileName.c_str(), std::ios_base::app | std::ios_base::out);
  
  //set utf8 if file is not exist
  if (!isExist)
  {
    const unsigned char bom[] = { 0xEF,0xBB,0xBF };
    infoFileWriter.write((char*)bom, sizeof(bom));
    MessageTrace( "replay_writer.WriteStartGameInfo: file does not exist - creating new");
  }
  else
  {
    MessageTrace( "replay_writer.WriteStartGameInfo: file is exist - adding line");
    infoFileWriter << "\n";
  }
    
  const std::string replayDataToEnc = 
	  _replayInfo.mapName + separator +
	  _replayInfo.heroId + separator +
	  currentDateTime();

  infoFileWriter << base64_encode(reinterpret_cast<const unsigned char*>(replayDataToEnc.c_str()), replayDataToEnc.length());
  infoFileWriter << separator;
  
  //if local game started (don`t from castle)
  if (headerWritten)
  {
    const std::string replayFilePathEnc(replayFilePath.c_str()); 
    infoFileWriter << utf8_encode(utf8_decode(replayFilePathEnc));
  }
    

  infoFileWriter.close();
  infoheaderWritten = true;
  MessageTrace( "replay_writer.WriteStartGameInfo: completed");
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReplayWriter::WriteFinishGame(int step, const StatisticService::RPC::SessionClientResults & _sessionResults, const NGameX::ReplayInfo & _replayInfo)
{
  NI_PROFILE_FUNCTION;

  if (!fileWorker || finished) // Possible in case we have failed to open the file
    return;

  NI_VERIFY(headerWritten, "Replay header wasn't written", return;);

  recordBuffer.clear();
  PutBytes(recordBuffer, &step, sizeof(step));
  unsigned short commandsCount = 0;
  unsigned short statusesCount = 0;
  PutBytes(recordBuffer, &commandsCount, sizeof(commandsCount));
  PutBytes(recordBuffer, &statusesCount, sizeof(statusesCount));
  fileWorker->Append(&recordBuffer[0], recordBuffer.size());

  // The worker writes out the tail and closes the file on its own thread
  finished = true;
  fileWorker->RequestClose();

  //write information
  WriteSessionInfoToFile(_sessionResults, _replayInfo);
}

//////////////
void ReplayWriter::WriteSessionInfoToFile(const StatisticService::RPC::SessionClientResults & _sessionResults, const NGameX::ReplayInfo & _replayInfo) {
  MessageTrace( "replay_writer.WriteSessionInfoToFile: start");

  const std::string infoFileNameStr(informationFileName.c_str());
  const std::wstring convertedToUtf8InfoFileName = utf8_decode(infoFileNameStr);

  //append win info to last line
  const std::string win_info = (_replayInfo.isWon ? "1" : "0");
  MessageTrace( "replay_writer.WriteSessionInfoToFile: start appending");
  std::ofstream infoFile;
  infoFile.open(convertedToUtf8InfoFileName.c_str(), std::ios_base::app | std::ios_base::out);
  
  const std::string statsStr = NStr::StrFmt("%d,%d,%d", _replayInfo.kills, _replayInfo.assists, _replayInfo.deaths);
  
  infoFile << separator << win_info;
  infoFile << separator << base64_encode(reinterpret_cast<const unsigned char*>(statsStr.c_str()), statsStr.length());
  infoFile << '\n';
  infoFile.close();
  
  MessageTrace( "replay_writer.WriteSessionInfoToFile: has appending completed");
}

} // namespace NCore


NI_DEFINE_REFCOUNT( NCore::ReplayWriter );

