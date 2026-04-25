#include "System/stdafx.h"
#include "System/ChannelLogger.h"
#include "System/InlineProfiler3/InlineProfiler3.h"
#include "System/LogStreamBuffer.h"
#include "System/SafeTextFormat.h"
#include "System/SafeTextFormatEx.h"
#include "System/SystemLog.h"
#include "System/Texts.h"
#include "Core/GameTypes.h"
#include "Client/Tooltips.h"
#include "Client/ScreenCommands.h"
#include "Game/PF/Client/LobbyPvx/NewReplay.h"
#include "PF_GameLogic/WebLauncher.h"
#include "PF_GameLogic/StringExecutorBootstrap.h"
#include "Render/NullRenderSignal.h"
#include "Render/TextureManager.h"
#include "Render/material.h"
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

#include <stdio.h>
#include <wchar.h>

std::map<nstl::wstring, WebLauncherPostRequest::WebUserData> g_usersData;
string g_devLogin;
bool g_needNotifyLobbyClients = false;
string g_selectedHeroes[10];

namespace
{
class LinuxBootstrapScreenCommand : public NScreenCommands::IScreenCommand
{
  NI_DECLARE_REFCOUNT_CLASS_1(LinuxBootstrapScreenCommand, NScreenCommands::IScreenCommand);

public:
  virtual void Exec()
  {
  }

  virtual bool Prepare()
  {
    return true;
  }
};
}

namespace NMainLoop
{
namespace
{
float g_linuxBootstrapTime = 0.0f;
float g_linuxBootstrapTimeDelta = 1.0f / 60.0f;
}

void UpdateTime()
{
  g_linuxBootstrapTime += g_linuxBootstrapTimeDelta;
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
  return g_linuxBootstrapTimeDelta;
}

float GetTimeScale()
{
  return 1.0f;
}

void SetTimeScale(float scale)
{
  (void)scale;
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

void AddTooltip(const wstring& text, const UI::STooltipDesc& desc)
{
  (void)text;
  (void)desc;
}
}

namespace NScreenCommands
{
IScreenCommand* CreatePushScreenCommand(NMainLoop::IScreenBase* pScreenToPush)
{
  (void)pScreenToPush;
  return new LinuxBootstrapScreenCommand;
}

IScreenCommand* CreatePopScreenCommand(NMainLoop::IScreenBase* pScreenToPop)
{
  (void)pScreenToPop;
  return new LinuxBootstrapScreenCommand;
}

IScreenCommand* CreatePopScreenCommand(const string& screenToPopName)
{
  (void)screenToPopName;
  return new LinuxBootstrapScreenCommand;
}

IScreenCommand* CreateClearStackCommand()
{
  return new LinuxBootstrapScreenCommand;
}

void RegisterInScreensFactory(const string& name, CreateFun createFun)
{
  (void)name;
  (void)createFun;
}

NMainLoop::IScreenBase* CreateScreen(const string& name)
{
  (void)name;
  return 0;
}

void PushCommand(IScreenCommand* pCommand)
{
  (void)pCommand;
}

bool ProcessScreenCmds()
{
  return false;
}

bool AnalizeScreenCmds()
{
  return false;
}

void ClearCommands()
{
}
}

namespace NCore
{
ReplayWriter::ReplayWriter()
  : versionWritten(false),
    lobbyDataWritten(false),
    gsDataWritten(false),
    headerWritten(false),
    infoheaderWritten(false)
{
}

void ReplayWriter::WriteVersion(const Login::ClientVersion& _clientVersion)
{
  (void)_clientVersion;
}

void ReplayWriter::WriteLobbyData(Transport::TClientId clientId, const lobby::TGameLineUp& _gameLineUp, const lobby::SGameParameters& _gameParams)
{
  (void)clientId;
  (void)_gameLineUp;
  (void)_gameParams;
}

void ReplayWriter::WriteGSData(int stepLength, const ClientSettings& clientSettings, const vector<Peered::ClientInfo>& clientInfos)
{
  (void)stepLength;
  (void)clientSettings;
  (void)clientInfos;
}

void ReplayWriter::WriteStartGame(Peered::TSessionId serverId, int step)
{
  (void)serverId;
  (void)step;
}

void ReplayWriter::WriteStepData(int step, const nstl::vector<rpc::MemoryBlock>& commands, const vector<Peered::BriefClientInfo>& statuses)
{
  (void)step;
  (void)commands;
  (void)statuses;
}

void ReplayWriter::WriteStartGameInfo(const NGameX::ReplayInfo& _replayInfo)
{
  (void)_replayInfo;
}

void ReplayWriter::WriteFinishGame(int step, const StatisticService::RPC::SessionClientResults& _sessionResults, const NGameX::ReplayInfo& _replayInfo)
{
  (void)step;
  (void)_sessionResults;
  (void)_replayInfo;
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
int g_playerHeroId = 0;
int g_playerPartyId = 0;
int g_playersCount = 0;
std::string g_protocolToken;
bool g_localGameRun = false;
string g_mapId;

template<>
const CObjectBase* CastToObjectBaseImpl<NScene::SceneComponent>(const NScene::SceneComponent* p, const void*)
{
  return p ? p->CastToObjectBase() : 0;
}

template<>
CObjectBase* CastToObjectBaseImpl<NScene::SceneComponent>(NScene::SceneComponent* p, void*)
{
  return p ? p->CastToObjectBase() : 0;
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

namespace NDb
{
struct AdvMapDescription;
struct Texture;
}

namespace NRandom
{
class RandomGenerator;
}

namespace NLogg
{
CLogger::CLogger(const char* _szName)
  : szName(_szName),
    headerFormat(EHeaderFormat::Default)
{
}

CLogger::~CLogger()
{
}

void CLogger::Log(const SEntryInfo&, const char*, const char*)
{
  // The Linux bootstrap writes its own structured status/log output elsewhere.
}

CChannelLogger::~CChannelLogger()
{
}
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

namespace UI
{
namespace
{
class LinuxBootstrapAlphabet : public IAlphabet, public BaseObjectST
{
  NI_DECLARE_REFCOUNT_CLASS_2(LinuxBootstrapAlphabet, IAlphabet, BaseObjectST);

public:
  LinuxBootstrapAlphabet()
  {
    glyph.u1 = 0.0f;
    glyph.v1 = 0.0f;
    glyph.u2 = 1.0f;
    glyph.v2 = 1.0f;
    glyph.width = 8.0f;
    glyph.height = 18.0f;
    glyph.offsetW = 0.0f;
    glyph.offsetH = 0.0f;
    glyph.A = 0.0f;
    glyph.B = 8.0f;
    glyph.C = 0.0f;
  }

  virtual const Glyph& GetGlyph(wchar_t symbol)
  {
    const float width = symbol == L' ' ? 5.0f : 8.0f;
    glyph.width = width;
    glyph.B = width;
    return glyph;
  }

  virtual void SetupMetric(SFontMetric& metric, float gapAbove, float gapUnder, const SFontRenderTweaks& tweaks) const
  {
    metric.ascent = 14.0f;
    metric.descent = 4.0f;
    metric.gapAbove = gapAbove;
    metric.gapUnder = gapUnder;
    metric.tweaks = tweaks;
    metric.defaultGlyphWidth = 8.0f;
  }

  virtual float GetStringLength(const wchar_t* text, unsigned length, float maxWidth, unsigned* charsFitIn, const SFontRenderTweaks& tweaks)
  {
    const float defaultWidth = 8.0f + tweaks.additionalAdvance;
    const float spaceWidth = 5.0f * tweaks.spaceScale + tweaks.additionalAdvance;
    float currentWidth = 0.0f;
    unsigned fitCount = 0;

    for (unsigned i = 0; i < length; ++i)
    {
      const float glyphWidth = text && text[i] == L' ' ? spaceWidth : defaultWidth;
      if (maxWidth > 0.0f && (currentWidth + glyphWidth) > maxWidth)
      {
        break;
      }

      currentWidth += glyphWidth;
      ++fitCount;
    }

    if (charsFitIn)
    {
      *charsFitIn = fitCount;
    }

    if (maxWidth <= 0.0f && text)
    {
      for (unsigned i = fitCount; i < length; ++i)
      {
        currentWidth += text[i] == L' ' ? spaceWidth : defaultWidth;
      }
    }

    return currentWidth;
  }

  virtual void DrawString(const wchar_t* text, unsigned length, float x, float y, Render::BaseMaterial* fontMaterial, const Render::Color& color, const Rect& cropRect, const SFontRenderTweaks& tweaks, const CVec2& scale)
  {
    (void)text;
    (void)length;
    (void)x;
    (void)y;
    (void)fontMaterial;
    (void)color;
    (void)cropRect;
    (void)tweaks;
    (void)scale;
  }

  virtual float GetHeightScale() const
  {
    return 1.0f;
  }

private:
  mutable Glyph glyph;
};

class LinuxBootstrapFontStyle : public IFontStyle, public BaseObjectST
{
  NI_DECLARE_REFCOUNT_CLASS_2(LinuxBootstrapFontStyle, IFontStyle, BaseObjectST);

public:
  explicit LinuxBootstrapFontStyle(IAlphabet* bootstrapAlphabet)
    : uiFontStyle(0),
      alphabet(bootstrapAlphabet),
      modified(true)
  {
    SFontRenderTweaks tweaks;
    alphabet->SetupMetric(metric, 0.5f, 0.5f, tweaks);
  }

  void SetStyle(const NDb::UIFontStyle* style)
  {
    uiFontStyle = style;

    SFontRenderTweaks tweaks;
    if (uiFontStyle)
    {
      tweaks.additionalAdvance = uiFontStyle->additionalAdvance;
      tweaks.spaceScale = uiFontStyle->spaceScale;
    }

    alphabet->SetupMetric(metric, 0.5f, 0.5f, tweaks);
    modified = true;
  }

  virtual const NDb::UIFontStyle* GetStyle() const
  {
    return uiFontStyle;
  }

  virtual Render::BaseMaterial* GetMaterial()
  {
    return 0;
  }

  virtual bool CheckModified()
  {
    const bool wasModified = modified;
    modified = false;
    return wasModified;
  }

  virtual void DrawString(const wchar_t* text, unsigned length, float x, float y, const Render::Color& color, const Rect& cropRect)
  {
    alphabet->DrawString(text, length, x, y, 0, color, cropRect, metric.tweaks, CVec2(1.0f, 1.0f));
  }

  virtual float GetStringLength(const wchar_t* text, unsigned length)
  {
    return alphabet->GetStringLength(text, length, 0.0f, 0, metric.tweaks);
  }

  virtual IAlphabet* GetAlphabet()
  {
    return alphabet;
  }

  virtual const SFontMetric& GetMetric() const
  {
    return metric;
  }

private:
  const NDb::UIFontStyle* uiFontStyle;
  Strong<IAlphabet> alphabet;
  SFontMetric metric;
  bool modified;
};

class LinuxBootstrapFontRenderer : public IFontRenderer, public BaseObjectST
{
  NI_DECLARE_REFCOUNT_CLASS_2(LinuxBootstrapFontRenderer, IFontRenderer, BaseObjectST);

public:
  LinuxBootstrapFontRenderer()
    : defaultAlphabet(new LinuxBootstrapAlphabet)
  {
  }

  virtual void Initialize()
  {
  }

  virtual void Release()
  {
    fontStyles.clear();
    debugFontStyles.clear();
    fontsTexture = Render::Texture2DRef();
  }

  virtual Render::Texture2DRef& GetFontsTexture()
  {
    return fontsTexture;
  }

  virtual IAlphabet* FindNearestAlphabet(const char* ttfFileName, int size, bool systemFont, bool bold, bool italic, bool forFlash)
  {
    (void)ttfFileName;
    (void)size;
    (void)systemFont;
    (void)bold;
    (void)italic;
    (void)forFlash;
    return defaultAlphabet;
  }

  virtual const Rect& GetNoCropRect()
  {
    static Rect noCropRect(0.0f, 0.0f, 0.0f, 0.0f);
    return noCropRect;
  }

  virtual IFontStyle* GetFontStyle(const NDb::UIFontStyle* style)
  {
    if (!style)
    {
      return GetDebugFontStyle(20);
    }

    Strong<LinuxBootstrapFontStyle>& slot = fontStyles[style];
    if (!slot)
    {
      slot = new LinuxBootstrapFontStyle(defaultAlphabet);
    }

    slot->SetStyle(style);
    return slot;
  }

  virtual IFontStyle* GetDebugFontStyle(int size)
  {
    Strong<LinuxBootstrapFontStyle>& slot = debugFontStyles[size];
    if (!slot)
    {
      slot = new LinuxBootstrapFontStyle(defaultAlphabet);
      slot->SetStyle(0);
    }

    return slot;
  }

private:
  Strong<LinuxBootstrapAlphabet> defaultAlphabet;
  Render::Texture2DRef fontsTexture;
  map<const NDb::UIFontStyle*, Strong<LinuxBootstrapFontStyle> > fontStyles;
  map<int, Strong<LinuxBootstrapFontStyle> > debugFontStyles;
};
}

IFontRenderer* GetFontRenderer()
{
  static Strong<LinuxBootstrapFontRenderer> fontRenderer = new LinuxBootstrapFontRenderer;
  return fontRenderer;
}

NScript::Script* GetUIScript()
{
  return NScript::GetScript(0);
}

void ReleaseUIScript()
{
}

void AddScriptFile(const string& scriptFile)
{
  (void)scriptFile;
}
}

NI_DEFINE_REFCOUNT(UI::IAlphabet)
NI_DEFINE_REFCOUNT(UI::IFontStyle)
NI_DEFINE_REFCOUNT(UI::LinuxBootstrapAlphabet)
NI_DEFINE_REFCOUNT(UI::LinuxBootstrapFontStyle)
NI_DEFINE_REFCOUNT(UI::LinuxBootstrapFontRenderer)

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

namespace NLogg
{
void CChannelLogger::Log(const SEntryInfo& entryInfo, const char* headerAndText, const char* textOnly)
{
  CLogger::Log(entryInfo, headerAndText, textOnly);
}

void StreamBuffer::WriteHeader(unsigned)
{
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

namespace text
{
size_t FormatArray(IBuffer* buffer, const char* fmt, const IArg* const*, size_t)
{
  if (!buffer || !fmt)
  {
    return 0;
  }

  return buffer->Write(fmt, strlen(fmt));
}

void BasicArg::FormatString(IBuffer* buffer, const char* str, const SFormatSpecs&)
{
  if (buffer && str)
  {
    buffer->Write(str, strlen(str));
  }
}

void BasicArg::FormatString(IBuffer* buffer, const wchar_t* str, const SFormatSpecs&)
{
  if (!buffer || !str)
  {
    return;
  }

  string mbcs;
  NStr::ToMBCS(&mbcs, wstring(str));
  buffer->Write(mbcs.c_str(), mbcs.length());
}

char* BasicArg::SafeAppend(char* buff, const char* buffEnd, const char* src)
{
  if (!buff || !src || buff >= buffEnd)
  {
    return 0;
  }

  while (*src)
  {
    if (buff + 1 >= buffEnd)
    {
      return 0;
    }

    *buff++ = *src++;
  }

  *buff = 0;
  return buff;
}

char* BasicArg::SafeAppend(char* buff, const char* buffEnd, char c)
{
  if (!buff || buff + 1 >= buffEnd)
  {
    return 0;
  }

  *buff++ = c;
  *buff = 0;
  return buff;
}

char* BasicArg::FormatFormat(char* fmt, size_t fmtBufffSize, const SFormatSpecs& specs)
{
  if (!fmt || fmtBufffSize < 2)
  {
    return 0;
  }

  char* ptr = fmt;
  const char* end = fmt + fmtBufffSize;
  *ptr++ = '%';
  *ptr = 0;

  if (specs.flags & EFlags::Minus)
  {
    ptr = SafeAppend(ptr, end, '-');
  }
  if (ptr && (specs.flags & EFlags::Plus))
  {
    ptr = SafeAppend(ptr, end, '+');
  }
  if (ptr && (specs.flags & EFlags::Zero))
  {
    ptr = SafeAppend(ptr, end, '0');
  }
  if (ptr && (specs.flags & EFlags::Sharp))
  {
    ptr = SafeAppend(ptr, end, '#');
  }
  if (ptr && (specs.flags & EFlags::Blank))
  {
    ptr = SafeAppend(ptr, end, ' ');
  }

  if (ptr && (specs.flags & EFlags::Has_Width))
  {
    char width[32];
    snprintf(width, sizeof(width), "%d", specs.width);
    ptr = SafeAppend(ptr, end, width);
  }

  if (ptr && (specs.flags & EFlags::Has_Precision))
  {
    char precision[32];
    snprintf(precision, sizeof(precision), ".%d", specs.precision);
    ptr = SafeAppend(ptr, end, precision);
  }

  return ptr;
}

char (&GetThreadBuffer())[THREAD_BUFF_SZ]
{
  static thread_local char buffer[THREAD_BUFF_SZ];
  buffer[0] = 0;
  return buffer;
}
}

namespace Input
{
int GetVerbosityLevel()
{
  return 0;
}
}

NLogg::CChannelLogger& GetSystemLog()
{
  static NLogg::CChannelLogger* g_systemLog = new NLogg::CChannelLogger("System");
  return *g_systemLog;
}

void TraceMsg(const char* msg)
{
  if (msg && msg[0])
  {
    fprintf(stderr, "%s\n", msg);
  }
}

bool G_IsRandomBotSkinsEnabled()
{
  return false;
}

namespace Render
{
Material* CreateRenderMaterial(const NDb::Material* pDbMaterial)
{
  (void)pDbMaterial;
  return 0;
}

Material* CreateRenderMaterial(const int typeId)
{
  (void)typeId;
  return 0;
}

void UnloadTexturePool(void* poolId)
{
  (void)poolId;
}

void OnTextureDestruction(Texture* tex)
{
  (void)tex;
}

Texture2DRef Create2DTextureFromArray2D(const CArray2D<Render::Color>& src)
{
  if (src.IsEmpty())
  {
    return Texture2DRef();
  }

  D3DSURFACE_DESC desc = D3DSURFACE_DESC();
  desc.Format = D3DFMT_A8R8G8B8;
  desc.Type = D3DRTYPE_TEXTURE;
  desc.Usage = 0;
  desc.Pool = D3DPOOL_MANAGED;
  desc.MultiSampleType = D3DMULTISAMPLE_NONE;
  desc.MultiSampleQuality = 0;
  desc.Width = src.GetSizeX();
  desc.Height = src.GetSizeY();
  return Create<Texture2D>(desc);
}

Texture2DRef LoadTexture2DIntoPool(const NDb::Texture&, bool, void*)
{
  return Texture2DRef();
}

Texture2DRef LoadTexture2D(const NDb::Texture&)
{
  return Texture2DRef();
}
}

namespace NSoundScene
{
FMOD::Event* EventStart(const NDb::DBFMODEventDesc& eventDesc)
{
  (void)eventDesc;
  return 0;
}

FMOD::Event* EventStart(const NDb::DBFMODEventDesc& eventDesc, const CVec3& position)
{
  (void)eventDesc;
  (void)position;
  return 0;
}
}

namespace UI
{
namespace Debug
{
bool MouseTraceEnabled()
{
  return false;
}

void AddRect(const Rect& rect, const Color& color, float duration)
{
  (void)rect;
  (void)color;
  (void)duration;
}
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
