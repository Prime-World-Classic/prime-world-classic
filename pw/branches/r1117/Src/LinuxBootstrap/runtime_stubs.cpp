#include "System/stdafx.h"
#include "System/ChannelLogger.h"
#include "System/Dumper.h"
#include "System/InlineProfiler3/InlineProfiler3.h"
#include "System/LogStreamBuffer.h"
#include "System/SafeTextFormat.h"
#include "System/SafeTextFormatEx.h"
#include "System/FileSystem/FileWriteAsynchronousStream.h"
#include "System/ImageTGA.h"
#include "System/Crc32Checksum.h"
#include "System/StrProc.h"
#include "System/SystemLog.h"
#include "System/Texts.h"
#include "Core/BaseState.h"
#include "Core/GameTypes.h"
#include "Core/WorldBase.h"
#include "Client/MainLoop.h"
#include "Client/Tooltips.h"
#include "Client/ScreenCommands.h"
#include "Server/RPC/Types.h"
#include "Game/PF/Client/LobbyPvx/NewReplay.h"
#include "PF_GameLogic/AdventureScreen.h"
#include "PF_GameLogic/DBAdvMap.h"
#include "PF_GameLogic/DBGameLogic.h"
#include "PF_GameLogic/DBHeroesList.h"
#include "PF_GameLogic/MapStartup.h"
#include "PF_GameLogic/PFResourcesCollection.h"
#include "PF_GameLogic/PointersHolder.h"
#include "PF_GameLogic/WebLauncher.h"
#include "PF_GameLogic/StringExecutorBootstrap.h"
#include "Render/NullRenderSignal.h"
#include "Render/TextureManager.h"
#include "Render/debugrenderer.h"
#include "Render/material.h"
#include "Render/smartrenderer.h"
#include "Render/texture.h"
#include "Scene/SceneComponent.h"
#include "Scripts/Script.h"
#include "Scripts/LuaCommon.h"
#include "Scripts/LuaComplexTypes.h"
#include "Scripts/LuaConstants.h"
#include "Scripts/LuaSubclass.h"
#include "Scripts/LuaTableLinks.h"
#include "Sound/EventScene.h"
#include "UI/DebugDraw.h"
#include "UI/FontRender.h"
#include "UI/LuaEventResult.h"
#include "UI/Scripts.h"
#include "System/MainFrame.h"
#include "UI/ImageComponent.h"
#include "libdb/XmlSaver.h"

#include <cstdlib>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <wchar.h>

void* Aligned_MAlloc(size_t size, size_t alignment)
{
  if (alignment < sizeof(void*))
    alignment = sizeof(void*);

  void* result = 0;
  if (posix_memalign(&result, alignment, size) != 0)
    return 0;

  return result;
}

void Aligned_Free(void* ptr)
{
  std::free(ptr);
}

std::map<nstl::wstring, WebLauncherPostRequest::WebUserData> g_usersData;
string g_devLogin;
bool g_needNotifyLobbyClients = false;
string g_selectedHeroes[10];

namespace NMainLoop
{
namespace
{
float g_linuxBootstrapTime = 0.0f;
float g_linuxBootstrapTimeDelta = 1.0f / 60.0f;
float g_linuxBootstrapTimeScale = 1.0f;
}

void UpdateTime()
{
  g_linuxBootstrapTime += g_linuxBootstrapTimeDelta * g_linuxBootstrapTimeScale;
}

void MarkStepFrame()
{
}

float GetTime()
{
  return g_linuxBootstrapTime;
}

NHPTimer::STime GetHPTime()
{
  return 0;
}

float GetTimeDelta()
{
  return g_linuxBootstrapTimeDelta * g_linuxBootstrapTimeScale;
}

float GetTimeScale()
{
  return g_linuxBootstrapTimeScale;
}

void SetTimeScale(float scale)
{
  g_linuxBootstrapTimeScale = scale;
}

void SetTemporaryTimeDelta(float val)
{
  g_linuxBootstrapTimeDelta = val > 0.0f ? val : (1.0f / 60.0f);
}

void SetTemporaryTime(float val)
{
  g_linuxBootstrapTime = val;
}

bool IsAppActive()
{
  return NMainFrame::IsAppActive();
}
}

namespace NCore
{
namespace
{
const char kLinuxReplayMagic[] = "PWLXREPLAY1\n";
const char kLinuxReplayInfoMagic[] = "PWLXREPLAYINFO1";
const char kLinuxReplayFolder[] = "logs";
const char kLinuxReplayPath[] = "logs/linux-bootstrap-replay.pwrp";
const char kLinuxReplayInfoPath[] = "logs/linux-bootstrap-replay.pwrp.info";

void EnsureLinuxReplayFolder()
{
  mkdir(kLinuxReplayFolder, 0775);
}
}

ReplayWriter::ReplayWriter()
  : versionWritten(false),
    lobbyDataWritten(false),
    gsDataWritten(false),
    headerWritten(false),
    infoheaderWritten(false),
    linuxReplayFile(0),
    linuxReplayInfoFilePath(kLinuxReplayInfoPath),
    linuxReplayInfoHeaderWritten(false),
    linuxReplayStartWrites(0),
    linuxReplayStepWrites(0),
    linuxReplayCommandWrites(0),
    linuxReplayStatusWrites(0),
    linuxReplayBytesWritten(0),
    linuxReplayWriteFailures(0)
{
}

ReplayWriter::~ReplayWriter()
{
  if (linuxReplayFile)
  {
    fflush(linuxReplayFile);
    fclose(linuxReplayFile);
    linuxReplayFile = 0;
  }
}

void ReplayWriter::LinuxEnsureReplayFile()
{
  if (linuxReplayFile)
  {
    return;
  }

  EnsureLinuxReplayFolder();
  replaysFolderPath = kLinuxReplayFolder;
  replayFilePath = kLinuxReplayPath;
  linuxReplayFile = fopen(replayFilePath.c_str(), "wb");
  if (!linuxReplayFile)
  {
    ++linuxReplayWriteFailures;
  }
}

void ReplayWriter::LinuxWriteReplay(const void* data, size_t size)
{
  if (!data || size == 0)
  {
    return;
  }

  if (!linuxReplayFile)
  {
    ++linuxReplayWriteFailures;
    return;
  }

  if (fwrite(data, 1, size, linuxReplayFile) == size)
  {
    linuxReplayBytesWritten += size;
  }
  else
  {
    ++linuxReplayWriteFailures;
  }
}

void ReplayWriter::WriteVersion(const Login::ClientVersion& _clientVersion)
{
  (void)_clientVersion;
  versionWritten = true;
}

void ReplayWriter::WriteLobbyData(Transport::TClientId clientId, const lobby::TGameLineUp& _gameLineUp, const lobby::SGameParameters& _gameParams)
{
  (void)clientId;
  (void)_gameLineUp;
  (void)_gameParams;
  lobbyDataWritten = true;
}

void ReplayWriter::WriteGSData(int stepLength, const ClientSettings& clientSettings, const vector<Peered::ClientInfo>& clientInfos)
{
  (void)stepLength;
  (void)clientSettings;
  (void)clientInfos;
  gsDataWritten = true;
}

void ReplayWriter::WriteLinuxBootstrapHeader(const MapStartInfo& mapStartInfo, int clientId, int stepLength, const ClientSettings& clientSettings)
{
  EnsureLinuxReplayFolder();
  linuxReplayInfoFilePath = kLinuxReplayInfoPath;

  FILE* infoFile = fopen(linuxReplayInfoFilePath.c_str(), "wb");
  if (!infoFile)
  {
    ++linuxReplayWriteFailures;
    linuxReplayInfoHeaderWritten = false;
    return;
  }

  fprintf(infoFile, "%s\n", kLinuxReplayInfoMagic);
  fprintf(infoFile, "clientId\t%d\n", clientId);
  fprintf(infoFile, "stepLength\t%d\n", stepLength);
  fprintf(infoFile, "mapDescName\t%s\n", mapStartInfo.mapDescName.c_str());
  fprintf(infoFile, "replayName\t%s\n", mapStartInfo.replayName.c_str());
  fprintf(infoFile, "randomSeed\t%d\n", mapStartInfo.randomSeed);
  fprintf(infoFile, "isCustomGame\t%d\n", mapStartInfo.isCustomGame ? 1 : 0);
  fprintf(infoFile, "minigameEnabled\t%d\n", clientSettings.minigameEnabled ? 1 : 0);
  fprintf(infoFile, "logicParam1\t%.6f\n", static_cast<double>(clientSettings.logicParam1));
  fprintf(infoFile, "aiForLeaversEnabled\t%d\n", clientSettings.aiForLeaversEnabled ? 1 : 0);
  fprintf(infoFile, "aiForLeaversThreshold\t%d\n", clientSettings.aiForLeaversThreshold);
  fprintf(infoFile, "playerCount\t%lu\n", static_cast<unsigned long>(mapStartInfo.playersInfo.size()));
  for (size_t i = 0; i < mapStartInfo.playersInfo.size(); ++i)
  {
    const PlayerStartInfo& player = mapStartInfo.playersInfo[i];
    const string playerNickname = NStr::ToMBCS(player.nickname);
    fprintf(
      infoFile,
      "player\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%u\t%s\t%s\t%u\t%d\t%.6f\t%d\t%d\t%d\t%u\n",
      player.playerID,
      static_cast<int>(player.teamID),
      static_cast<int>(player.originalTeamID),
      static_cast<int>(player.playerType),
      player.userID,
      static_cast<int>(player.zzimaSex),
      playerNickname.c_str(),
      static_cast<unsigned int>(player.playerInfo.heroId),
      player.playerInfo.heroSkin.c_str(),
      player.playerInfo.locale.c_str(),
      static_cast<unsigned int>(player.playerInfo.heroLevel),
      player.playerInfo.heroExp,
      static_cast<double>(player.playerInfo.heroRating),
      player.playerInfo.hasPremium ? 1 : 0,
      static_cast<int>(player.playerInfo.basket),
      player.playerInfo.isAnimatedAvatar ? 1 : 0,
      static_cast<unsigned int>(player.playerInfo.partyId)
    );
  }

  fclose(infoFile);
  linuxReplayInfoHeaderWritten = true;
}

void ReplayWriter::WriteStartGame(Peered::TSessionId serverId, int step)
{
  LinuxEnsureReplayFile();
  if (!linuxReplayFile)
  {
    return;
  }

  LinuxWriteReplay(kLinuxReplayMagic, sizeof(kLinuxReplayMagic) - 1);
  LinuxWriteReplay(&serverId, sizeof(serverId));
  LinuxWriteReplay(&step, sizeof(step));
  fflush(linuxReplayFile);
  headerWritten = true;
  ++linuxReplayStartWrites;
}

void ReplayWriter::WriteStepData(int step, const nstl::vector<rpc::MemoryBlock>& commands, const vector<Peered::BriefClientInfo>& statuses)
{
  if (!headerWritten)
  {
    WriteStartGame(0, step);
  }

  if (!linuxReplayFile)
  {
    return;
  }

  LinuxWriteReplay(&step, sizeof(step));

  const size_t commandsToWrite = Min<size_t>(commands.size(), 0xFFFF);
  const size_t statusesToWrite = Min<size_t>(statuses.size(), 0xFFFF);
  unsigned short commandsCount = static_cast<unsigned short>(commandsToWrite);
  unsigned short statusesCount = static_cast<unsigned short>(statusesToWrite);
  LinuxWriteReplay(&commandsCount, sizeof(commandsCount));
  LinuxWriteReplay(&statusesCount, sizeof(statusesCount));

  for (size_t i = 0; i < commandsToWrite; ++i)
  {
    const rpc::MemoryBlock& block = commands[i];
    const size_t bytesToWrite = Min<size_t>(block.size, 0xFFFF);
    unsigned short size = static_cast<unsigned short>(bytesToWrite);
    LinuxWriteReplay(&size, sizeof(size));
    LinuxWriteReplay(block.memory, bytesToWrite);
  }

  for (size_t i = 0; i < statusesToWrite; ++i)
  {
    const Peered::BriefClientInfo& status = statuses[i];
    unsigned short size = static_cast<unsigned short>(sizeof(status));
    LinuxWriteReplay(&size, sizeof(size));
    LinuxWriteReplay(&status, sizeof(status));
  }

  fflush(linuxReplayFile);
  ++linuxReplayStepWrites;
  linuxReplayCommandWrites += commandsToWrite;
  linuxReplayStatusWrites += statusesToWrite;
}

void ReplayWriter::WriteStartGameInfo(const NGameX::ReplayInfo& _replayInfo)
{
  (void)_replayInfo;
  infoheaderWritten = true;
}

void ReplayWriter::WriteFinishGame(int step, const StatisticService::RPC::SessionClientResults& _sessionResults, const NGameX::ReplayInfo& _replayInfo)
{
  (void)_sessionResults;
  (void)_replayInfo;
  if (!headerWritten || !linuxReplayFile)
  {
    return;
  }

  unsigned short zero = 0;
  LinuxWriteReplay(&step, sizeof(step));
  LinuxWriteReplay(&zero, sizeof(zero));
  LinuxWriteReplay(&zero, sizeof(zero));
  fflush(linuxReplayFile);
}

void ReplayWriter::WriteSessionInfoToFile(const StatisticService::RPC::SessionClientResults& _sessionResults, const NGameX::ReplayInfo& _replayInfo)
{
  (void)_sessionResults;
  (void)_replayInfo;
}
}

extern "C"
{
int lua_gc(lua_State* L, int what, int data)
{
  (void)L;
  (void)what;
  (void)data;
  return 0;
}

int lua_gettop(lua_State* L)
{
  (void)L;
  return 0;
}

int lua_toboolean(lua_State* L, int idx)
{
  (void)L;
  (void)idx;
  return 0;
}

void lua_pushnil(lua_State* L)
{
  (void)L;
}

void lua_pushboolean(lua_State* L, int value)
{
  (void)L;
  (void)value;
}

void lua_pushnumber(lua_State* L, double n)
{
  (void)L;
  (void)n;
}

void lua_settop(lua_State* L, int idx)
{
  (void)L;
  (void)idx;
}

const char* lua_tolstring(lua_State* L, int idx, size_t* len)
{
  (void)L;
  (void)idx;
  if (len)
  {
    *len = 0;
  }
  return "";
}

int lua_type(lua_State* L, int idx)
{
  (void)L;
  (void)idx;
  return 0;
}
}

IMPLEMENT_SIMPLE_SIGNAL_ST(NullRenderSignal)

string g_sessionName;
WebLauncherPostRequest::RegisterSessionRequest g_sessionStatus = WebLauncherPostRequest::RegisterInSessionRequest_Wait;
int g_playerTeamId = 0;
int g_fixedTeamCam = 0;
int g_playerHeroId = 0;
int g_playerPartyId = 0;
int g_playersCount = 0;
std::string g_protocolToken;
bool g_localGameRun = false;
string g_mapId;

namespace NCore
{
class WorldCommand;
}

namespace NWorld
{
class Target;

const char* MakeTargetString(const Target&)
{
  return "<linux-bootstrap-target>";
}

namespace
{
const NDb::Hero* FindHeroInOverride(const NDb::AdvMapHeroesOverrideData* data, uint heroId)
{
  if (!data)
    return 0;

  if (data->ownHero.hero && Crc32Checksum().AddString(data->ownHero.hero->id.c_str()).Get() == heroId)
    return data->ownHero.hero;

  for (vector<NDb::AdvMapPlayerData>::const_iterator it = data->allies.begin(); it != data->allies.end(); ++it)
    if (it->hero && Crc32Checksum().AddString(it->hero->id.c_str()).Get() == heroId)
      return it->hero;

  for (vector<NDb::AdvMapPlayerData>::const_iterator it = data->enemies.begin(); it != data->enemies.end(); ++it)
    if (it->hero && Crc32Checksum().AddString(it->hero->id.c_str()).Get() == heroId)
      return it->hero;

  return 0;
}
}

const NDb::Hero* FindHero(const NDb::HeroesDB* db, const NDb::AdvMapDescription* advMapDesc, uint heroId)
{
  if (db)
  {
    for (int i = 0; i < db->heroes.size(); ++i)
    {
      if (db->heroes[i] && Crc32Checksum().AddString(db->heroes[i]->id.c_str()).Get() == heroId)
        return db->heroes[i];
    }
  }

  if (advMapDesc && advMapDesc->heroesOverride)
  {
    if (const NDb::Hero* hero = FindHeroInOverride(advMapDesc->heroesOverride->singlePlayerMale, heroId))
      return hero;
    if (const NDb::Hero* hero = FindHeroInOverride(advMapDesc->heroesOverride->singlePlayerFemale, heroId))
      return hero;
  }

  return 0;
}
}

template<>
const CObjectBase* CastToObjectBaseImpl<Render::Texture>(const Render::Texture* p, const void*)
{
  return p ? p->CastToObjectBase() : 0;
}

template<>
CObjectBase* CastToObjectBaseImpl<Render::Texture>(Render::Texture* p, void*)
{
  return p ? p->CastToObjectBase() : 0;
}

template<>
const CObjectBase* CastToObjectBaseImpl<FileWriteAsynchronousStream>(const FileWriteAsynchronousStream* p, const void*)
{
  return p ? p->CastToObjectBase() : 0;
}

template<>
CObjectBase* CastToObjectBaseImpl<FileWriteAsynchronousStream>(FileWriteAsynchronousStream* p, void*)
{
  return p ? p->CastToObjectBase() : 0;
}

template<>
CObjectBase* CastToObjectBaseImpl<NCore::IWorldBase>(NCore::IWorldBase* p, void*)
{
  return p ? p->CastToObjectBase() : 0;
}

template<>
CObjectBase* CastToObjectBaseImpl<NWorld::PFResourcesCollection>(NWorld::PFResourcesCollection* p, void*)
{
  return p ? p->CastToObjectBase() : 0;
}

namespace NWorld
{
_interface IPointerHolder;
class TileMap;
class PFBaseUnit;
class PFHeroStatistics;
class MapLoadingController;
}

namespace PF_Minigames
{
class IMinigamesMain;
}

namespace Pathfinding
{
class CCommonPathFinder;
class RoutePathFinder;
}

template<>
CObjectBase* CastToObjectBaseImpl<NWorld::IPointerHolder>(NWorld::IPointerHolder*, void*)
{
  return 0;
}

#define PW_LINUX_BOOTSTRAP_OBJECT_CASTS(TypeName) \
template<> CObjectBase* CastToObjectBaseImpl<TypeName>(TypeName*, void*) { return 0; } \
template<> TypeName* CastToUserObjectImpl<TypeName>(CObjectBase*, TypeName*, void*) { return 0; } \
template<> TypeName* CastToUserObjectImpl<TypeName>(CObjectBase*, TypeName*, CObjectBase*) { return 0; }

PW_LINUX_BOOTSTRAP_OBJECT_CASTS(NWorld::PFHeroStatistics)
PW_LINUX_BOOTSTRAP_OBJECT_CASTS(PF_Minigames::IMinigamesMain)
PW_LINUX_BOOTSTRAP_OBJECT_CASTS(Pathfinding::CCommonPathFinder)
PW_LINUX_BOOTSTRAP_OBJECT_CASTS(Pathfinding::RoutePathFinder)

#undef PW_LINUX_BOOTSTRAP_OBJECT_CASTS

namespace ni_detail
{
template<>
WeakPointerProxyST* AcquireWeakProxyImpl<WeakPointerProxyST, NGameX::AdventureScreen>(NGameX::AdventureScreen* p, void*)
{
  return p ? p->AcquireWeakProxy() : 0;
}
}

namespace NCore
{
namespace
{
class LinuxBootstrapInvalidFSMState : public IBaseFSMState
{
public:
  virtual void DestroyContents()
  {
  }

  virtual void DeleteThis()
  {
  }

  virtual const int IsInvalidRef() const
  {
    return 1;
  }

  virtual CObjectBase* CastToObjectBase()
  {
    return 0;
  }

  virtual const CObjectBase* CastToObjectBase() const
  {
    return 0;
  }

  virtual const int GetTypeId() const
  {
    return -1;
  }

  virtual const char* GetObjectTypeName() const
  {
    return "LinuxBootstrapInvalidFSMState";
  }

  virtual void Init()
  {
  }

  virtual IBaseFSMState* Step(float)
  {
    return 0;
  }
};
}
}

template<>
NCore::IBaseFSMState* GetInvalid<NCore::IBaseFSMState>()
{
  static NCore::LinuxBootstrapInvalidFSMState invalidState;
  return &invalidState;
}

namespace Render
{
CVec2& GetAOEScaleHACK()
{
  static CVec2 scale(0.0f, 0.0f);
  return scale;
}

CVec2& GetAOEOffsetHACK()
{
  static CVec2 offset(0.0f, 0.0f);
  return offset;
}
}

namespace NDb
{
struct AdvMapDescription;
struct Texture;
}

namespace NRandom
{
class RandomGenerator;
}

namespace NScript
{
namespace
{
Script* GetLinuxBootstrapScriptSingleton()
{
  static Script* script = new Script;
  return script;
}
}

Script::Script()
  : state(0),
    ownState(false),
    isInited(true),
    isReadOnly(false),
    calculateCrc(false),
    crc(0)
{
}

Script::Script(bool _calculateCrc)
  : state(0),
    ownState(false),
    isInited(true),
    isReadOnly(false),
    calculateCrc(_calculateCrc),
    crc(0)
{
}

Script::~Script()
{
}

void Script::Reinit()
{
  loadedFiles.clear();
}

int Script::DoString(const string& str)
{
  (void)str;
  return 0;
}

int Script::DoFile(const string& str)
{
  (void)str;
  return 0;
}

void Script::RegisterGlobals()
{
}

bool Script::StartThread(lua_State* pParentState, int numberOfArguments)
{
  (void)pParentState;
  (void)numberOfArguments;
  return false;
}

void Script::StepLuaThreads()
{
}

void AddSFunctionToGlobals(const char* fname, lua_CFunction func)
{
  (void)fname;
  (void)func;
}

Script* GetScript(lua_State* pLuaState)
{
  (void)pLuaState;
  return GetLinuxBootstrapScriptSingleton();
}
}

namespace Lua
{
bool CheckStackParameterIdx(lua_State* L, int idx)
{
  (void)L;
  (void)idx;
  return false;
}

bool RetrieveObjectMehtod(lua_State* L, char const* name)
{
  (void)L;
  (void)name;
  return false;
}

bool MakePreparedCall(lua_State* L, char const* name, int nParams, int nResults)
{
  (void)L;
  (void)name;
  (void)nParams;
  (void)nResults;
  return false;
}

bool GetObjectByStrongRef(lua_State* L, void* object)
{
  (void)L;
  (void)object;
  return false;
}

void KillStrongRef(lua_State* L, void* object)
{
  (void)L;
  (void)object;
}

bool GetObjectByWeakRef(lua_State* L, void const* object)
{
  (void)L;
  (void)object;
  return false;
}

void KillWeakRef(lua_State* L, void* pObject)
{
  (void)L;
  (void)pObject;
}

void ConnectMetatable(lua_State* L, char const* className, LuaTypeInfo const* pTypeInfo, void* pObject)
{
  (void)L;
  (void)className;
  (void)pTypeInfo;
  (void)pObject;
}

void MetatableToStack(lua_State* L, char const* className, LuaTypeInfo const* pTypeInfo)
{
  (void)L;
  (void)className;
  (void)pTypeInfo;
}

int PutWeakObjectToStack(lua_State* L, char const* className, LuaTypeInfo const* pTypeInfo, void* pObject)
{
  (void)L;
  (void)className;
  (void)pTypeInfo;
  (void)pObject;
  return 0;
}

void DropDeadObjectTable(lua_State* L)
{
  (void)L;
}

bool FindObjectMehtod(lua_State* L, char const* name)
{
  (void)L;
  (void)name;
  return false;
}

void TableLinkToTable(lua_State* L, TableLinkBase* pLink)
{
  (void)L;
  (void)pLink;
}

void UserdataToObject(lua_State* L, void* object)
{
  (void)L;
  (void)object;
}

bool FindSubtable(lua_State* L, char const* name)
{
  (void)L;
  (void)name;
  return false;
}

bool PrepareSubtablePath(lua_State* L, char const* name)
{
  (void)L;
  (void)name;
  return false;
}

bool PrepareTablePath(lua_State* L, char const* name)
{
  (void)L;
  (void)name;
  return false;
}

void* ObjectPtrFromMeta(lua_State* L, void* TargetClass, int nStackPos)
{
  (void)L;
  (void)TargetClass;
  (void)nStackPos;
  return 0;
}

void* ObjectPtrFromMetaCall(lua_State* L, void* TargetClass)
{
  (void)L;
  (void)TargetClass;
  return 0;
}

string GetLuaCallStack(lua_State* pState, int startLevel)
{
  (void)pState;
  (void)startLevel;
  return string();
}

StackChecker::StackChecker(lua_State* pState, int offset)
{
  (void)pState;
  (void)offset;
}

StackChecker::~StackChecker()
{
}

void OnEnterLuaScript()
{
}

void OnLeaveLuaScript()
{
}

void OnEnterLuaNative()
{
}

void OnLeaveLuaNative()
{
}

TableLinkBase::TableLinkBase(lua_State* pLuaState)
  : pLuaState(pLuaState)
{
}

TableLinkBase::~TableLinkBase()
{
}

bool TableLinkBase::Has(char const* subtable, char const* name) const
{
  (void)subtable;
  (void)name;
  return false;
}

bool TableLinkBase::PrepareHandlerCall(char const* name)
{
  (void)name;
  return false;
}

bool TableLinkBase::MakeHandlerCall(char const* name, int nParams, int nResults)
{
  (void)name;
  (void)nParams;
  (void)nResults;
  return false;
}

bool ParameterTableLink::MyTableToStack() const
{
  return false;
}

ObjectTableLink::ObjectTableLink(lua_State* pLuaState)
  : TableLinkBase(pLuaState),
    pObject(0)
{
}

ObjectTableLink::~ObjectTableLink()
{
}

bool ObjectTableLink::MyTableToStack() const
{
  return false;
}

LuaSubclass::LuaSubclass()
  : pLuaState(0)
{
}

LuaSubclass::~LuaSubclass()
{
}

bool LuaSubclass::Subclass(lua_State* pState, string const& subclass, void* pThis)
{
  (void)pThis;
  pLuaState = pState;
  subclassName = subclass;
  return false;
}

bool LuaSubclass::UnSubclass()
{
  pLuaState = 0;
  subclassName.clear();
  pTable = AutoPtr<ObjectTableLink>();
  return true;
}

LuaSimpleDefine::LuaSimpleDefine(const char* prefix, char const* name, char const* value)
{
  (void)prefix;
  (void)name;
  (void)value;
}

LuaSimpleDefine::LuaSimpleDefine(const char* prefix, char const* name, int value)
{
  (void)prefix;
  (void)name;
  (void)value;
}

bool lua_values<bool>::get(lua_State* L, int idx)
{
  (void)L;
  (void)idx;
  return false;
}

int lua_values<bool>::put(lua_State* L, bool value)
{
  (void)L;
  (void)value;
  return 1;
}

float lua_values<float>::get(lua_State* L, int idx)
{
  (void)L;
  (void)idx;
  return 0.0f;
}

int lua_values<float>::put(lua_State* L, float value)
{
  (void)L;
  (void)value;
  return 1;
}

int lua_values<int>::get(lua_State* L, int idx)
{
  (void)L;
  (void)idx;
  return 0;
}

int lua_values<int>::put(lua_State* L, int value)
{
  (void)L;
  (void)value;
  return 1;
}

const char* lua_values<const char*>::get(lua_State* L, int idx)
{
  (void)L;
  (void)idx;
  return "";
}

int lua_values<const char*>::put(lua_State* L, const char* value)
{
  (void)L;
  (void)value;
  return 1;
}

string lua_values<string const&>::get(lua_State* L, int idx)
{
  (void)L;
  (void)idx;
  return string();
}

int lua_values<string const&>::put(lua_State* L, string const& value)
{
  (void)L;
  (void)value;
  return 1;
}

string lua_values<string>::get(lua_State* L, int idx)
{
  (void)L;
  (void)idx;
  return string();
}

int lua_values<string>::put(lua_State* L, string const& value)
{
  (void)L;
  (void)value;
  return 1;
}

wstring lua_values<const wstring&>::get(lua_State* L, int idx)
{
  (void)L;
  (void)idx;
  return wstring();
}

int lua_values<const wstring&>::put(lua_State* L, wstring const& value)
{
  (void)L;
  (void)value;
  return 1;
}

wstring lua_values<wstring>::get(lua_State* L, int idx)
{
  (void)L;
  (void)idx;
  return wstring();
}

int lua_values<wstring>::put(lua_State* L, wstring const& value)
{
  (void)L;
  (void)value;
  return 1;
}

CTPoint<float> lua_values<CTPoint<float> >::get(lua_State* L, int idx)
{
  (void)L;
  (void)idx;
  return CTPoint<float>(0.0f, 0.0f);
}

int lua_values<CTPoint<float> >::put(lua_State* L, const CTPoint<float>& value)
{
  (void)L;
  (void)value;
  return 1;
}

CTRect<float> lua_values<CTRect<float> >::get(lua_State* L, int idx)
{
  (void)L;
  (void)idx;
  return CTRect<float>(0.0f, 0.0f, 0.0f, 0.0f);
}

int lua_values<CTRect<float> >::put(lua_State* L, const CTRect<float>& value)
{
  (void)L;
  (void)value;
  return 1;
}

int lua_values<NScript::NamedValues>::put(lua_State* L, const NScript::NamedValues& value)
{
  (void)L;
  (void)value;
  return 1;
}

LuaSubclass* lua_values<LuaSubclass*>::get(lua_State* L, int idx)
{
  (void)L;
  (void)idx;
  return 0;
}

int lua_values<LuaSubclass*>::put(lua_State* L, LuaSubclass* value)
{
  (void)L;
  (void)value;
  return 1;
}

LuaSubclass const* lua_values<LuaSubclass const*>::get(lua_State* L, int idx)
{
  (void)L;
  (void)idx;
  return 0;
}

int lua_values<LuaSubclass const*>::put(lua_State* L, LuaSubclass const* value)
{
  (void)L;
  (void)value;
  return 1;
}
}

namespace profiler3
{
void SetupThisThread(const char*)
{
}

void CleanupThisThread()
{
}

TEventId RegisterEvent(const char*, const char*, int)
{
  return 0;
}

void StartEvent(TEventId)
{
}

void FinishEvent(TEventId)
{
}

void StartEavyEvent(TEventId)
{
}

void FinishEavyEvent(TEventId)
{
}

void StartMemoryEvent(TEventId)
{
}

void FinishMemoryEvent(TEventId)
{
}
}

namespace Input
{
int GetVerbosityLevel()
{
  return 0;
}
}

bool G_IsRandomBotSkinsEnabled()
{
  return false;
}

namespace Compatibility
{
void Init()
{
}

bool IsRunnedUnderWine()
{
  return false;
}

bool IsRunnedUnderCrossOverWine()
{
  return false;
}
}

namespace NDebug
{
const wchar_t* GetProductNameW()
{
  return L"PrimeWorldLinuxClient";
}

nstl::string GenerateDebugFileName(const char* suffix, const char* extension, const char* folder, bool)
{
  nstl::string result;
  if (folder && folder[0])
  {
    result += folder;
    result += "/";
  }
  result += suffix ? suffix : "debug";
  if (extension && extension[0])
  {
    result += ".";
    result += extension;
  }
  return result;
}
}

namespace utils
{
bool GetMemoryStatus(size_t& virtualSize)
{
  virtualSize = 0;
  return false;
}
}

namespace NWorld
{
string GetRandomHeroSkin(uint, const NDb::AdvMapDescription*, NRandom::RandomGenerator&, NCore::ETeam::Enum)
{
  return string();
}
}

void* ExecutableString::formulaCache = 0;

ExecutableString::ExecutableString()
  : pExecutor(0)
{
}

int ExecutableString::operator&(IBinSaver& saver)
{
  saver.Add(2, &sString);
  saver.Add(3, &compiledString);
  saver.Add(4, &returnType);
  return 0;
}

int ExecutableString::operator&(IXmlSaver& saver)
{
  saver.Add("sString", &sString);
  saver.Add("compiledString", &compiledString);
  saver.Add("returnType", &returnType);
  return 0;
}

ExecutableString& ExecutableString::operator=(const ExecutableString& other)
{
  if (this != &other)
  {
    sString = other.sString;
    compiledString = other.compiledString;
    returnType = other.returnType;
    pExecutor = 0;
  }
  return *this;
}

bool ExecutableString::GetVariantValue(NScript::VariantValue&, const char*) const
{
  return false;
}

void CTextRef::DropCache()
{
}

const wstring& CTextRef::GetText() const
{
  static wstring empty;
  return empty;
}
