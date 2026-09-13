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
#if !defined( NI_PLATF_LINUX )
  WORD _nFPUStatus;

  __asm 
  {
    fstcw _nFPUStatus
    wait
  }
#endif

  nFPUStatus = GetProcessorState();

  SetProcessorState( UI_PROCESSOR_STATE, 0xffffffff );
}

void FlashEnterFunction::LoadFloatState()
{
  SetProcessorState( nFPUStatus );
}

}
