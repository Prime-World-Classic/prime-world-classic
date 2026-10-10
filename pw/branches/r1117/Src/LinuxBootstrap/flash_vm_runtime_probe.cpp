#include "TamarinPCH.h"

#include "flash_vm_runtime_probe.h"
#include "UI/DBUI.h"
#include "UI/FlashContainer2.h"
#include "UI/FlashInterface.h"
#include "UI/FSCommandListner.h"
#include "UI/Flash/GameSWFIntegration/FlashMovie.h"
#include "UI/Flash/GameSWFIntegration/FlashMovieAvmCore.h"
#include "UI/Flash/GameSWFIntegration/FlashText.h"
#include "UI/Flash/GameSWFIntegration/FontsRenderInterface.h"
#include "UI/Flash/GameSWFIntegration/Natives/text/TextField.h"

#include <cstdio>

namespace
{
/// Deterministic font metrics keep TextField conversion tests independent of assets and a GL context.
class ProbeFont : public flash::IFontInstance, public BaseObjectST
{
	NI_DECLARE_REFCOUNT_CLASS_2(ProbeFont, flash::IFontInstance, BaseObjectST);
public:
	virtual void RenderGlyph(wchar_t, const flash::SWF_MATRIX&, const flash::SWF_RGBA&) {}
	virtual void RenderText(const wchar_t*, unsigned, const flash::SWF_MATRIX&, const flash::SWF_RGBA&, const flash::SWF_RECT&) {}
	virtual float GetStringLength(const wchar_t*, unsigned length, float maxWidth, unsigned* fit, float advance)
	{
		const float width = 4 + advance;
		const unsigned count = maxWidth > 0 ? Min(length, static_cast<unsigned>(maxWidth / width)) : length;
		if (fit)
			*fit = count;
		return count * width;
	}
	virtual float Height() const { return 10; }
	virtual float Ascent() const { return 7; }
	virtual float Descent() const { return 1; }
	virtual float GapAbove() const { return 1; }
	virtual float GapUnder() const { return 1; }
	virtual float DefaultGlyphWidth() const { return 4; }
	virtual void SetBevel(bool, const flash::SWF_RGBA&) {}
};

/// Supply the same fixed font to the production text container for each test field.
class ProbeFontRender : public flash::IFontRender, public BaseObjectST
{
	NI_DECLARE_REFCOUNT_CLASS_2(ProbeFontRender, flash::IFontRender, BaseObjectST);
public:
	virtual void SetViewport(int, int, int, int) {}
	virtual void SetMovieRect(float, float, float, float) {}
	virtual flash::IFontInstance* FindFont(const char*, int, bool, int, const flash::SFontMetricInfo*) { return new ProbeFont; }
	virtual void DebugLine(float, float, float, float, const flash::SWF_RGBA&) {}
};

/// Capture both encodings delivered by the production FSCommand listener path.
class FlashCommandProbe : public UI::IFSCommandListner, public BaseObjectST
{
	NI_DECLARE_REFCOUNT_CLASS_2(FlashCommandProbe, UI::IFSCommandListner, BaseObjectST);
public:
	FlashCommandProbe() : calls(0) {}
	virtual void OnFSCommand(UI::FlashContainer2*, const char* id, const char* args, const wchar_t* argsW)
	{
		++calls;
		command = id;
		utf8 = args;
		wide = argsW;
	}
	int calls;
	string command;
	string utf8;
	wstring wide;
};

/// Check callback text ownership and dispatch/removal without loading a SWF or opening a display.
bool ProbeFlashCommands(MMgc::GC* gc)
{
	Strong<ProbeFontRender> fonts = new ProbeFontRender;
	flash::Movie movie(0, gc, fonts, 0);
	Strong<FlashCommandProbe> listener = new FlashCommandProbe;
	movie.AddFSListner(0, "text-probe", listener);
	avmplus::AvmCore* core = movie.GetAvmCore();
	avmplus::Stringp command = core->newStringLatin1("text-probe");
	const string utf8 = "Bot \xD0\x93\xD0\xB5\xD1\x80\xD0\xBE\xD0\xB9 \xF0\x9F\x98\x80";
	movie.OnFSCommand(command, core->newStringUTF8(utf8.c_str(), utf8.size()));
	const bool text = listener->calls == 1 && listener->command == "text-probe" && listener->utf8 == utf8 &&
		listener->wide == L"Bot \u0413\u0435\u0440\u043e\u0439 \U0001F600";
	movie.OnFSCommand(command, core->newStringLatin1(""));
	const bool empty = listener->calls == 2 && listener->utf8.empty() && listener->wide.empty();
	movie.RemoveFSListner("text-probe");
	movie.OnFSCommand(command, core->newStringLatin1("ignored"));
	const bool removed = listener->calls == 2;
	std::printf("Flash command probe: text=%s empty=%s removed=%s\n",
		text ? "yes" : "NO", empty ? "yes" : "NO", removed ? "yes" : "NO");
	avmplus::ClassClosure* textClass = movie.GetAvmCore()->GetClassByName("flash.text.TextField");
	avmplus::Atom args[] = {avmplus::AtomConstants::nullObjectAtom};
	avmplus::TextFieldObject* field = dynamic_cast<avmplus::TextFieldObject*>(
		avmplus::AvmCore::atomToScriptObject(textClass->construct(0, args)));
	if (!field)
		return false;
	field->set_width(1000);
	field->set_height(100);
	field->set_text(core->newStringUTF8(utf8.c_str(), utf8.size()));
	avmplus::StUTF8String caption(field->get_text());
	const bool captionMatch = string(caption.c_str(), caption.length()) == utf8;
	field->set_text(core->newStringLatin1(""));
	const bool emptyCaption = field->get_text()->length() == 0;
	std::printf("Flash TextField probe: caption=%s empty=%s\n", captionMatch ? "yes" : "NO", emptyCaption ? "yes" : "NO");
	return text && empty && removed && captionMatch && emptyCaption;
}

/// Exercise the real wide-string bridge with localized, supplementary, and bounded strings.
bool ProbeFlashWideStrings(flash::FlashMovieAvmCore* core)
{
	avmplus::ClassClosure* sprite = core->GetClassByName("flash.display.Sprite");
	if (!sprite)
		return false;
	nstl::vector<wstring> values;
	values.push_back(L"Bot Hero");
	values.push_back(L"\u0413\u0435\u0440\u043e\u0439");
	values.push_back(L"A\U0001F600Z");
	values.push_back(L"");
	values.push_back(wstring(L"A\0B", 3));
	const string expected[] = {"Bot Hero", "\xD0\x93\xD0\xB5\xD1\x80\xD0\xBE\xD0\xB9",
		"A\xF0\x9F\x98\x80Z", "", string("A\0B", 3)};
	avmplus::ArrayObject* array = static_cast<avmplus::ArrayObject*>(avmplus::AvmCore::atomToScriptObject(
		UI::FVar(values).GetAtom(sprite->toplevel())));
	if (!array)
		return false;
	bool passed = array->get_length() == static_cast<unsigned int>(values.size());
	for (int i = 0; i < values.size(); ++i)
	{
		avmplus::StUTF8String single(avmplus::AvmCore::atomToString(UI::FVar(values[i]).GetAtom(sprite->toplevel())));
		avmplus::StUTF8String element(avmplus::AvmCore::atomToString(array->getIntProperty(i)));
		const bool singleMatch = string(single.c_str(), single.length()) == expected[i];
		const bool arrayMatch = string(element.c_str(), element.length()) == expected[i];
		const bool reverseMatch = flash::CreateWideStringFromAvm(
			core->newStringUTF8(expected[i].data(), expected[i].size())) == values[i];
		std::printf("Flash wide-string probe: case=%d single=%s array=%s reverse=%s\n", i,
			singleMatch ? "yes" : "NO", arrayMatch ? "yes" : "NO", reverseMatch ? "yes" : "NO");
		passed = singleMatch && arrayMatch && reverseMatch && passed;
	}
	return passed;
}
}

bool RunPrimeWorldLinuxFlashVmRuntimeProbe()
{
  MMgc::GCHeap::EnterLockInit();
  MMgc::GCHeapConfig config;
  MMgc::GCHeap::Init(config);

  MMgc::GCHeap* heap = MMgc::GCHeap::GetGCHeap();
  volatile bool initialized = false;
  volatile bool initializationError = false;
  size_t loadedPools = 0;
  bool eventClass = false;
  bool spriteClass = false;
  bool textFieldClass = false;
  bool byteArrayClass = false;
	bool wideStrings = false;
	bool commands = false;

  if (heap)
  {
    MMgc::GC gc(heap, MMgc::GC::kIncrementalGC);
    MMGC_GCENTER(&gc);
    {
      flash::FlashMovieAvmCore* core = new flash::FlashMovieAvmCore(&gc);
      using avmplus::Exception;
      using avmplus::ExceptionFrame;

      TRY(core, avmplus::kCatchAction_ReportAsError)
      {
        core->Initialize();
        initialized = true;
      }
      CATCH(Exception* exception)
      {
        initializationError = true;
        core->PrintException(exception);
      }
      END_CATCH
      END_TRY

      if (initialized)
      {
        for (int pool = 0; pool < PoolType::Last; ++pool)
        {
          if (core->GetBuiltinPool(static_cast<PoolType::Type>(pool)))
            ++loadedPools;
        }

        eventClass = core->GetClassByName("flash.events.Event") != 0;
        spriteClass = core->GetClassByName("flash.display.Sprite") != 0;
        textFieldClass = core->GetClassByName("flash.text.TextField") != 0;
        byteArrayClass = core->GetClassByName("flash.utils.ByteArray") != 0;
				wideStrings = ProbeFlashWideStrings(core);
      }

      delete core;
			if (initialized)
				commands = ProbeFlashCommands(&gc);
    }
    gc.Collect(false);
  }

  const size_t leakedBytes = MMgc::GCHeap::Destroy();
  MMgc::GCHeap::EnterLockDestroy();
  const size_t expectedPools = static_cast<size_t>(PoolType::Last);
  const bool passed = heap && initialized && !initializationError &&
    loadedPools == expectedPools && eventClass && spriteClass &&
    textFieldClass && byteArrayClass && wideStrings && commands && leakedBytes == 0;

  std::printf(
    "Flash VM runtime probe: initialized=%s pools=%zu/%zu event=%s sprite=%s "
    "textField=%s byteArray=%s wideStrings=%s commands=%s leakedBytes=%zu\n",
    initialized ? "yes" : "no",
    loadedPools,
    expectedPools,
    eventClass ? "yes" : "no",
    spriteClass ? "yes" : "no",
    textFieldClass ? "yes" : "no",
    byteArrayClass ? "yes" : "no",
		wideStrings ? "yes" : "no",
		commands ? "yes" : "no",
    leakedBytes);

  return passed;
}

bool RunPrimeWorldLinuxFlashUiHostProbe()
{
  bool factory = false;
  bool runtime = false;
  bool advanced = false;

  {
    NDb::Ptr<NDb::UIFlashLayout2> layout = new NDb::UIFlashLayout2;
    Strong<UI::Window> window = UI::CreateUIWindow(layout, 0, 0, 0, 0, false);
    UI::FlashContainer2* flashWindow = dynamic_cast<UI::FlashContainer2*>(window.Get());
    factory = flashWindow != 0;

    if (flashWindow)
    {
      runtime = flashWindow->IsRuntimeReadyForBootstrapProbe();
      flashWindow->SetManualMode(true);
      flashWindow->AdvanceOneFrame();
      advanced = true;
    }
  }

  std::printf(
    "Flash UI host probe: factory=%s runtime=%s advanced=%s teardown=yes\n",
    factory ? "yes" : "no",
    runtime ? "yes" : "no",
    advanced ? "yes" : "no");

  return factory && runtime && advanced;
}
