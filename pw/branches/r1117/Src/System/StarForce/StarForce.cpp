#include "stdafx.h"
#include "StarForce.h"

#pragma code_seg(push, "~")

#ifdef STARFORCE_PROTECTED
  #include "../AsyncInvoker.h"
#endif


namespace Protection
{
#ifdef STARFORCE_PROTECTED

static const unsigned __int32 kPscStatusSuccess = 0x00000000;

typedef unsigned __int32 (__stdcall *PSA_CheckProtectedModulesReadOnlyMem_t)( bool *memCheckResultOk );
typedef unsigned __int32 (__stdcall *PSA_CheckSystemLibsLocation_t)( char* outBuffer, size_t* outBufferSizePtr, bool* systemLibLocationChangedPtr );
typedef unsigned __int32 (__stdcall *PSA_CheckSystemLibsIat_t)( char* outBuffer, size_t* outBufferSizePtr, bool* systemLibIatModifiedPtr );
typedef unsigned __int32 (__stdcall *PSA_CheckSystemLibsReadOnlySections_t)( char* outBuffer, size_t* outBufferSizePtr, bool* readOnlySectionsModifiedPtr );

struct StarForceApi
{
  HMODULE module;
  PSA_CheckProtectedModulesReadOnlyMem_t checkProtectedModulesReadOnlyMem;
  PSA_CheckSystemLibsLocation_t checkSystemLibsLocation;
  PSA_CheckSystemLibsIat_t checkSystemLibsIat;
  PSA_CheckSystemLibsReadOnlySections_t checkSystemLibsReadOnlySections;
  bool initialized;
  bool available;
};

static StarForceApi g_starForceApi = { 0 };
static bool g_unavailableLogged = false;

class CallProxy
{
  SeparateThreadFuncPtr funcPtr;
  const void *pData;  

public: 
  CallProxy( SeparateThreadFuncPtr funcPtr = 0, const void *pData = 0 ):
    funcPtr(funcPtr), pData(pData) 
  {}
  
  void operator()() const
  {
    (*funcPtr)( pData );
  }
};

struct AsyncInvokerPriority: threading::AsyncInvoker<CallProxy>
{
  AsyncInvokerPriority()
  {
    SetPriority( THREAD_PRIORITY_BELOW_NORMAL );
  }
};

static AsyncInvokerPriority g_invoker;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool IsProtectionDisabledForCurrentRuntime()
{
  // Winlator works through Wine and StarForce initialization is not supported there.
  return Compatibility::IsRunnedUnderWine();
}

static bool ResolveStarForceApi()
{
  if( g_starForceApi.initialized )
    return g_starForceApi.available;

  g_starForceApi.initialized = true;

  if( IsProtectionDisabledForCurrentRuntime() )
  {
    g_starForceApi.available = false;
    return false;
  }

  g_starForceApi.module = GetModuleHandleA( "sakijapi.dll" );
  if( !g_starForceApi.module )
    g_starForceApi.module = LoadLibraryA( "sakijapi.dll" );

  if( !g_starForceApi.module )
  {
    g_starForceApi.available = false;
    return false;
  }

  g_starForceApi.checkProtectedModulesReadOnlyMem =
    (PSA_CheckProtectedModulesReadOnlyMem_t)GetProcAddress( g_starForceApi.module, "PSA_CheckProtectedModulesReadOnlyMem" );
  g_starForceApi.checkSystemLibsLocation =
    (PSA_CheckSystemLibsLocation_t)GetProcAddress( g_starForceApi.module, "PSA_CheckSystemLibsLocation" );
  g_starForceApi.checkSystemLibsIat =
    (PSA_CheckSystemLibsIat_t)GetProcAddress( g_starForceApi.module, "PSA_CheckSystemLibsIat" );
  g_starForceApi.checkSystemLibsReadOnlySections =
    (PSA_CheckSystemLibsReadOnlySections_t)GetProcAddress( g_starForceApi.module, "PSA_CheckSystemLibsReadOnlySections" );

  g_starForceApi.available =
    g_starForceApi.checkProtectedModulesReadOnlyMem &&
    g_starForceApi.checkSystemLibsLocation &&
    g_starForceApi.checkSystemLibsIat &&
    g_starForceApi.checkSystemLibsReadOnlySections;

  return g_starForceApi.available;
}

static bool EnsureProtectionAvailable( const char *funcName )
{
  if( ResolveStarForceApi() )
    return true;

  if( !g_unavailableLogged )
  {
    WarningTrace( "StarForce disabled: API is unavailable for current runtime" );
    g_unavailableLogged = true;
  }

  STARFORCE_LOG( "Implementation call skip (api unavailable): %s", funcName );
  return false;
}

static STARFORCE_FORCE_INLINE void CheckForTerminate( int line, bool result, unsigned __int32 status = kPscStatusSuccess )
{
  if( status != kPscStatusSuccess || !result )
  {
    ErrorTrace( "Protection Error. Status: 0x%08X, line: %d", status, line );
    int volatile * volatile p = 0;
    *p = 0; 

    exit( 0xDeadBeef ); 
  }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static STARFORCE_FORCE_INLINE void CheckForLog( int line, bool result, unsigned __int32 status = kPscStatusSuccess )
{
  if( status != kPscStatusSuccess || !result )
    WarningTrace( "Protection Warning. Status: 0x%08X, line: %d", status, line );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

STARFORCE_EXPORT void CheckReadOnlyAndExecutableImpl( const void * )
{
  if( !EnsureProtectionAvailable( __FUNCTION__ ) )
    return;

  STARFORCE_STOPWATCH();

  //По совету поддержки Старфорс воспользовался утилитой StripReloc и скорость работы 
  //PSA_CheckProtectedModulesReadOnlyMem возросла более чем в 50 раз
  //http://www.jrsoftware.org/striprlc.php

  bool checkResult = false;
  const unsigned __int32 status  = g_starForceApi.checkProtectedModulesReadOnlyMem( &checkResult );

  // NUM_TASK вызов CheckForTerminate заменен на CheckForLog
  // проверка может запуститься одновременно обоими способами. если проверка провалится, должна быть возможности отправить что-то на сервер,
  // поэтому "аварийное завершение работы" неуместно.
  CheckForLog( __LINE__, checkResult, status );  
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
STARFORCE_EXPORT void CheckSystemDllsImpl( const void * )
{
  if( !EnsureProtectionAvailable( __FUNCTION__ ) )
    return;

  STARFORCE_STOPWATCH();
  
  //На результат перечисленных ниже функций полагаться нельзя. Например,
  //PSA_CheckSystemLibsReadOnlySections срабатывает при установки 
  //антивируса Avast. 
  //Из переписки со StarForce:
  //
  // > И, наконец, есть ли ещё функции защиты, результат которых носит 
  // > вероятностный характер, но этого не отражено в документации? 
  //
  // В нашей системы нет вероятностных функций, но есть функции, результат 
  // работы которых не стоит использовать для аварийного завершения работы
  // приложение, так как они могут срабатывать и без наличия вредоносного ПО. 
  // Они служат источниками дополнительной информации о предполагаемой атаке. 
  // Это следующие функции: PSA_CheckSystemLibsLocation может вызвать ложное
  // срабатывание на экзотически сконфигурированных системах, например на 
  // нестандартных дистрибутивах Windows. 
  // PSA_CheckSystemLibsIat и PSA_CheckSystemLibsReadOnlySections могут также 
  // вызвать ложное срабатывание, при наличии системного ПО (например, антивирус).
  {
    bool systemLibLocationChanged = true;
    const unsigned __int32 status  = g_starForceApi.checkSystemLibsLocation( NULL, NULL, &systemLibLocationChanged );
    CheckForLog( __LINE__, !systemLibLocationChanged, status ); 
  }
  
  {
    bool systemLibIatModifiedPtr = true;
    const unsigned __int32 status  = g_starForceApi.checkSystemLibsIat( NULL, NULL, &systemLibIatModifiedPtr );
    CheckForLog( __LINE__, !systemLibIatModifiedPtr, status ); 
  }
  
  #ifdef OPTION_ENABLE_SYSTEM_LIBS_CODE_SECTION_CHECK
  {
    bool readOnlySectionsModified = true;
    const unsigned __int32 status  = g_starForceApi.checkSystemLibsReadOnlySections( NULL, NULL, &readOnlySectionsModified );
    CheckForLog( __LINE__, !readOnlySectionsModified, status ); 
  } 
  #endif
}
#endif
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

STARFORCE_EXPORT void CheckReadOnlyAndExecutable()
{
  #ifdef STARFORCE_PROTECTED
    //Если просто вызвать BeginInvoke, то возможна ситуация, когда
    //прошлая задача для потока ещё не выполнена и в таком случае 
    //основной поток будет остановлен до момента завершения 
    //существующей задачи. Для того чтобы это не создавало лагов
    //будем просто игнорировать новую задачу защиты, благо её 
    //выполнение не отражается на логике игры
    if( !g_invoker.IsBusy() )
      g_invoker.BeginInvoke( CheckReadOnlyAndExecutableImpl ); 
    else
      STARFORCE_LOG( "Implementation call skip: %s",  __FUNCTION__ );  
  #endif 
}

STARFORCE_EXPORT bool CheckReadOnlyAndExecutableImmediate()
{
#ifdef STARFORCE_PROTECTED
  if( !EnsureProtectionAvailable( __FUNCTION__ ) )
    return true;

  STARFORCE_STOPWATCH();

  bool checkResult = false;

  const unsigned __int32 status  = g_starForceApi.checkProtectedModulesReadOnlyMem( &checkResult );

  CheckForLog(__LINE__, checkResult, status);

  if (status != kPscStatusSuccess)
    return false;
  if (checkResult != true)
    return false;
#endif

  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

STARFORCE_EXPORT void CheckSystemDlls()
{
  #ifdef STARFORCE_PROTECTED
    if( !g_invoker.IsBusy() ) //см. выше
      g_invoker.BeginInvoke( CheckSystemDllsImpl );
    else
      STARFORCE_LOG( "Implementation call skip: %s",  __FUNCTION__ );  
  #endif 
}

STARFORCE_EXPORT void CallFunctionInProtectedSpace( SeparateThreadFuncPtr funcPtr, const void *pData )
{
  #ifdef STARFORCE_PROTECTED
    STARFORCE_STOPWATCH();
  
    if( !g_invoker.IsBusy() )
    { 
      g_invoker.BeginInvoke( CallProxy(funcPtr, pData) );
      g_invoker.EndInvoke();
    }
    else
    {
      //Мы не можем себе позволить просто игнорировать вызов funcPtr, также, как
      //это происходит с функциями защиты, поэтому просто вызовем эту функцию в 
      //основном потоке
      (*funcPtr)( pData );
    }
  #else
    (*funcPtr)( pData );
  #endif 

}

} //namespace Protection

#pragma code_seg(pop)
