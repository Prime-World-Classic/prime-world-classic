#include "TamarinPCH.h"

#include "flash_vm_runtime_probe.h"
#include "UI/DBUI.h"
#include "UI/FlashContainer2.h"
#include "UI/Flash/GameSWFIntegration/FlashMovieAvmCore.h"

#include <cstdio>

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
      }

      delete core;
    }
    gc.Collect(false);
  }

  const size_t leakedBytes = MMgc::GCHeap::Destroy();
  MMgc::GCHeap::EnterLockDestroy();
  const size_t expectedPools = static_cast<size_t>(PoolType::Last);
  const bool passed = heap && initialized && !initializationError &&
    loadedPools == expectedPools && eventClass && spriteClass &&
    textFieldClass && byteArrayClass && leakedBytes == 0;

  std::printf(
    "Flash VM runtime probe: initialized=%s pools=%zu/%zu event=%s sprite=%s "
    "textField=%s byteArray=%s leakedBytes=%zu\n",
    initialized ? "yes" : "no",
    loadedPools,
    expectedPools,
    eventClass ? "yes" : "no",
    spriteClass ? "yes" : "no",
    textFieldClass ? "yes" : "no",
    byteArrayClass ? "yes" : "no",
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
