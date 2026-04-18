#include "System/stdafx.h"
#include "System/ChannelLogger.h"
#include "System/InlineProfiler3/InlineProfiler3.h"
#include "System/LogStreamBuffer.h"
#include "System/SafeTextFormat.h"
#include "System/SafeTextFormatEx.h"
#include "System/SystemLog.h"
#include "System/Texts.h"
#include "Core/GameTypes.h"
#include "PF_GameLogic/StringExecutorBootstrap.h"
#include "Render/texture.h"
#include "Scene/SceneComponent.h"
#include "libdb/XmlSaver.h"

#include <stdio.h>
#include <wchar.h>

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
Texture2DRef LoadTexture2DIntoPool(const NDb::Texture&, bool, void*)
{
  return Texture2DRef();
}

Texture2DRef LoadTexture2D(const NDb::Texture&)
{
  return Texture2DRef();
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

const wstring& CTextRef::GetText() const
{
  static wstring empty;
  return empty;
}
