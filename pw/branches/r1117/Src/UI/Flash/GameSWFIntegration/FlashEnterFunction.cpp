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
  WORD _nFPUStatus;

#ifdef NV_LINUX_PLATFORM
  __asm__ __volatile__ ("fstcw %0\n\twait" : "=m" (_nFPUStatus));
#else
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
