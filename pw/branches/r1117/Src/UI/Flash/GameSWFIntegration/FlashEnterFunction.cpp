#include "TamarinPCH.h"

#include "FlashEnterFunction.h"
#include "FlashMovie.h"
#include <System/SyncProcessorState.h>

namespace flash
{

FlashEnterFunction::FlashEnterFunction()
{
  SaveFloatState();
}

FlashEnterFunction::~FlashEnterFunction()
{
  LoadFloatState();
}

void FlashEnterFunction::SaveFloatState()
{
  // __asm { fstcw _nFPUStatus } писал в локальную переменную, которая дальше не
  // читается (состояние берётся из GetProcessorState()) — на x64 убрано.
  nFPUStatus = GetProcessorState();

  SetProcessorState( UI_PROCESSOR_STATE, 0xffffffff );
}

void FlashEnterFunction::LoadFloatState()
{
  SetProcessorState( nFPUStatus );
}

}
