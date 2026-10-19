#ifndef _SYNC_PROCESSOR_STATE_H_
#define _SYNC_PROCESSOR_STATE_H_

static const DWORD LOGIC_PROCESSOR_STATE = _EM_INVALID | _EM_ZERODIVIDE | _EM_OVERFLOW | _EM_UNDERFLOW | _EM_INEXACT | _EM_DENORMAL | _PC_24	| _RC_NEAR;
static const DWORD UI_PROCESSOR_STATE = _EM_INVALID | _EM_ZERODIVIDE | _EM_OVERFLOW | _EM_UNDERFLOW | _EM_INEXACT | _EM_DENORMAL | _PC_64	| _RC_NEAR;

void SyncProcessorState();
unsigned int GetProcessorState();
void SetProcessorState( unsigned int newState, unsigned int mask = 0xffffffff );
bool IsProcessorStateForLogic();
bool IsProcessorStateForUI();


#if !defined( NI_PLATF_LINUX )

#if defined(_M_X64) || defined(__x86_64__)

// x64: MSVC не компилирует __asm вообще (C4235), а у x87-слова управлени€ там
// нет пол€ precision control (_PC_24/_PC_64 игнорируютс€ Ч арифметика SSE2
// всегда double). _control87 читает/восстанавливает то же состо€ние, с которым
// работают fstcw/fldcw на x86. —мысл блока (зафиксировать состо€ние FPU вокруг
// игрового шага) на x64 сохран€етс€ частично: rounding (_MCW_RC) и маски
// исключений общие дл€ x87 и SSE, precision control Ч нет.
#define NI_SYNC_FPU_START   \
  unsigned int nFPUStatus;  \
  nFPUStatus = (unsigned int)_control87( 0, 0 ); \
  SyncProcessorState();

#define NI_SYNC_FPU_END     \
  _control87( (unsigned int)nFPUStatus, _MCW_EM | _MCW_RC | _MCW_IC )

#else

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define NI_SYNC_FPU_START   \
  WORD nFPUStatus;          \
  __asm                     \
  {                         \
    __asm fstcw nFPUStatus  \
    __asm wait              \
  }                         \
  SyncProcessorState();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define NI_SYNC_FPU_END     \
  __asm                     \
  {                         \
    __asm fldcw nFPUStatus  \
    __asm wait              \
  }

#endif  // _M_X64

#endif  // !defined( NI_PLATF_LINUX )


namespace utils
{

bool GetMemoryStatus( size_t & virtualSize );
int GetThreadCount();

} //namespace utils

#endif  // _SYNC_PROCESSOR_STATE_H_
