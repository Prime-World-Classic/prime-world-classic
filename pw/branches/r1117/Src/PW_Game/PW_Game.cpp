#define _CRT_SECURE_NO_WARNINGS   // _snprintf in the crash diagnostics below
#include <windows.h>
#include <shlwapi.h>
#include <crtdbg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Diagnostics for __fastfail() exits (0xC0000409 STATUS_STACK_BUFFER_OVERRUN, and
// 0xC00000FD stack overflow). The CRT fastfail path bypasses our SEH filter AND WER
// LocalDumps, so the process just disappears: no exception.dmp, no C:\dumps, no reason.
// A first-chance vectored handler still runs before that, so log the fastfail sub-code
// (it says WHICH kind: /GS cookie, invalid CRT argument, pure virtual call, ...) plus the
// faulting RIP. Resolve RIP against the linker map (build with PW_EXTRA_LINK_OPTS="/MAP",
// then Tools/WineClientBuild/map_rvas.py). Log file sits next to the executable.
static const char *FastfailReason(unsigned c)
{
  switch (c)
  {
  case 0:  return "stack cookie check failure (/GS)";
  case 1:  return "buffer overrun detected";
  case 2:  return "invalid stack cookie";
  case 3:  return "__fastfail() called directly";
  case 4:  return "fatal app exit";
  case 5:  return "range check failure";
  case 6:  return "unsafe resource";
  case 7:  return "syscall violation";
  case 8:  return "invalid realloc";
  case 9:  return "incorrect stack";
  case 10: return "invalid argument (CRT parameter validator)";
  case 11: return "invalid binary";
  case 12: return "missing guard page";
  case 13: return "pure virtual call";
  case 14: return "newer system required";
  case 15: return "patching unsupported";
  case 16: return "delay-load: invalid function";
  case 17: return "unexpected / unrecognized";
  case 18: return "invalid dispose";
  case 19: return "invalid heap pointer";
  default: return "unknown";
  }
}

// FormulaVM post-mortem context, defined in Src/System/FormulaVM.cpp (x64 build only).
extern "C" {
  extern char const * g_formulaVmSource;
  extern char const * g_formulaVmAcc;
  extern unsigned __int64 g_formulaVmVals[8];
  extern int g_formulaVmSig;
  extern int g_formulaVmNArgs;
}

static LONG CALLBACK FastfailVeh(PEXCEPTION_POINTERS pi)
{
  const DWORD code = pi->ExceptionRecord->ExceptionCode;
  // AV and stack overflow are logged too: they often precede a fastfail exit and the
  // unhandled filter never gets a chance to write anything.
  if (code != 0xC0000409 && code != 0xC00000FD && code != 0xC0000005)
    return EXCEPTION_CONTINUE_SEARCH;

  char path[MAX_PATH];
  if (!GetModuleFileNameA(NULL, path, MAX_PATH - 16))
    return EXCEPTION_CONTINUE_SEARCH;
  char *slash = strrchr(path, '\\');
  if (!slash)
    return EXCEPTION_CONTINUE_SEARCH;
  lstrcpyA(slash, "\\fastfail.log");

  HANDLE f = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE)
    return EXCEPTION_CONTINUE_SEARCH;
  SetFilePointer(f, 0, NULL, FILE_END);

  const DWORD n = pi->ExceptionRecord->NumberParameters;
  const unsigned __int64 i0 = n > 0 ? pi->ExceptionRecord->ExceptionInformation[0] : 0;
  const unsigned __int64 i1 = n > 1 ? pi->ExceptionRecord->ExceptionInformation[1] : 0;
  const unsigned __int64 i2 = n > 2 ? pi->ExceptionRecord->ExceptionInformation[2] : 0;
  const CONTEXT *c = pi->ContextRecord;

  char buf[512];
  int len = _snprintf(buf, sizeof(buf) - 2,
    "code=%08X addr=%016I64X sub=%I64u (%s) info1=%016I64X info2=%016I64X tick=%u\n"
#ifdef _WIN64
    "  rip=%016I64X rsp=%016I64X rbp=%016I64X rax=%016I64X rbx=%016I64X rcx=%016I64X rdx=%016I64X\n",
    (unsigned)code, (unsigned __int64)pi->ExceptionRecord->ExceptionAddress, i0,
    FastfailReason((unsigned)i0), i1, i2, GetTickCount(),
    (unsigned __int64)c->Rip, (unsigned __int64)c->Rsp, (unsigned __int64)c->Rbp,
    (unsigned __int64)c->Rax, (unsigned __int64)c->Rbx, (unsigned __int64)c->Rcx,
    (unsigned __int64)c->Rdx);
#else
    "  eip=%08X esp=%08X ebp=%08X eax=%08X ebx=%08X ecx=%08X edx=%08X\n",
    (unsigned)code, (unsigned __int64)pi->ExceptionRecord->ExceptionAddress, i0,
    FastfailReason((unsigned)i0), i1, i2, GetTickCount(),
    (unsigned)c->Eip, (unsigned)c->Esp, (unsigned)c->Ebp,
    (unsigned)c->Eax, (unsigned)c->Ebx, (unsigned)c->Ecx, (unsigned)c->Edx);
#endif
  if (len < 0)
    len = 0;
  if (len > (int)sizeof(buf) - 2)
    len = (int)sizeof(buf) - 2;
  buf[len++] = '\n';

  DWORD wr = 0;
  WriteFile(f, buf, (DWORD)len, &wr, NULL);

  // If the crash happened while a formula was running in the VM, say which formula and
  // which accessor/arguments were in play (an accessor may fault inside game code, e.g.
  // dynamic_cast on a bad argument, where no C++ handler can recover that context).
#ifdef _WIN64
  if (g_formulaVmAcc)
  {
    char fl[700];
    int k = -1;
    __try
    {
      k = _snprintf(fl, sizeof(fl) - 3, "formula=\"%s\" acc=%s sig=%d nargs=%d vals=",
                    g_formulaVmSource ? g_formulaVmSource : "?", g_formulaVmAcc,
                    g_formulaVmSig, g_formulaVmNArgs);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
      k = -1;
    }
    if (k < 0)
      k = _snprintf(fl, sizeof(fl) - 3, "formula=<unreadable> acc=%s sig=%d nargs=%d vals=",
                    g_formulaVmAcc, g_formulaVmSig, g_formulaVmNArgs);
    if (k > (int)sizeof(fl) - 3)
      k = (int)sizeof(fl) - 3;
    for (int i = 0; i < 8 && k < (int)sizeof(fl) - 20; ++i)
      k += _snprintf(fl + k, sizeof(fl) - k - 3, "%016I64X ", g_formulaVmVals[i]);
    fl[k++] = '\n';
    WriteFile(f, fl, (DWORD)k, &wr, NULL);
  }
#endif

  // Mini backtrace: scan the stack for pointers that fall inside the main module and log
  // them as RVAs (resolve with Tools/WineClientBuild/map_rvas.py <map> --rvas ...).
  char *mod = (char *)GetModuleHandleA(NULL);
  size_t imgSize = 0;
  __try
  {
    IMAGE_DOS_HEADER *dh = (IMAGE_DOS_HEADER *)mod;
    if (dh->e_magic == IMAGE_DOS_SIGNATURE)
      imgSize = ((IMAGE_NT_HEADERS *)(mod + dh->e_lfanew))->OptionalHeader.SizeOfImage;
  }
  __except (EXCEPTION_EXECUTE_HANDLER)
  {
    imgSize = 0;
  }

  if (imgSize)
  {
    char line[128];
    int n = _snprintf(line, sizeof(line) - 3, "mod=%016I64X size=%IX\n",
                      (unsigned __int64)(uintptr_t)mod, (unsigned __int64)imgSize);
    if (n > 0)
    {
      if (n > (int)sizeof(line) - 3) n = (int)sizeof(line) - 3;
      line[n++] = '\n';
      WriteFile(f, line, (DWORD)n, &wr, NULL);
    }
#ifdef _WIN64
    // Real backtrace via the PE unwind tables (.pdata) - no pdb needed. Addresses are
    // logged as RVAs: Tools/WineClientBuild/map_rvas.py <map> --rvas <...>
    // UNW_FLAG_NHANDLER: we only walk frames; EHANDLER would let RtlVirtualUnwind run
    // the crashed function's own handlers/scope-exits, which must not execute during
    // post-mortem logging (re-entrancy into the code that just crashed).
    __try
    {
      CONTEXT walk = *c;
      UNWIND_HISTORY_TABLE hist;
      memset(&hist, 0, sizeof(hist));
      for (int frame = 0; frame < 48; ++frame)
      {
        if (!walk.Rip)
          break;
        int m = _snprintf(line, sizeof(line) - 3, "frame[%d]=%08X",
                          frame, (unsigned)(walk.Rip - (DWORD64)(uintptr_t)mod));
        if (m > 0)
        {
          if (m > (int)sizeof(line) - 3) m = (int)sizeof(line) - 3;
          line[m++] = '\n';
          WriteFile(f, line, (DWORD)m, &wr, NULL);
        }
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION rf = RtlLookupFunctionEntry(walk.Rip, &imageBase, &hist);
        if (!rf)
        {
          // leaf function without unwind info: the return address sits at [rsp]
          DWORD64 ret = 0;
          __try { ret = *(DWORD64 *)walk.Rsp; } __except (EXCEPTION_EXECUTE_HANDLER) { ret = 0; }
          if (!ret || ret == walk.Rip)
            break;
          walk.Rip = ret;
          walk.Rsp += 8;
          continue;
        }
        PVOID handlerData = NULL;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, walk.Rip, rf, &walk, &handlerData,
                        &establisher, NULL);
        if (!walk.Rip || walk.Rip == (DWORD64)-1)
          break;
      }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
#endif
  }
  CloseHandle(f);
  return EXCEPTION_CONTINUE_SEARCH;
}

// One-line append to <exe dir>\fastfail.log without any CRT/heap dependency.
static void FastfailLogLine(const char *line)
{
  char path[MAX_PATH];
  if (!GetModuleFileNameA(NULL, path, MAX_PATH - 16))
    return;
  char *slash = strrchr(path, '\\');
  if (!slash)
    return;
  lstrcpyA(slash, "\\fastfail.log");
  HANDLE f = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE)
    return;
  SetFilePointer(f, 0, NULL, FILE_END);
  DWORD wr = 0;
  WriteFile(f, line, (DWORD)lstrlenA(line), &wr, NULL);
  CloseHandle(f);
}

static void __cdecl MyInvalidParamHandler(const wchar_t *exp, const wchar_t *file,
                                          unsigned line, unsigned column, uintptr_t reserved)
{
  (void)reserved;
  char buf[256];
  _snprintf(buf, sizeof(buf) - 2, "invalid parameter: expr=%S file=%S line=%u column=%u\n",
            exp ? exp : L"(null)", file ? file : L"(null)", line, column);
  buf[sizeof(buf) - 2] = '\n';
  FastfailLogLine(buf);
}

static void __cdecl MyPurecallHandler(void)
{
  FastfailLogLine("pure virtual call\n");
}


extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;

#pragma warning( disable: 4800 ) // warning C4800: 'type' : forcing value to bool 'true' or 'false' (performance warning)
#pragma warning( disable: 4100 ) // warning C4100: 'identifier' : unreferenced formal parameter
#pragma warning( disable: 4127 ) // warning C4127: conditional expression is constant

size_t StrLength( LPCTSTR str )
{
  return StrCSpn( str, TEXT( "" ) );
}

//#define PW_CREATE_TEST_PLUGIN_WINDOW

#ifdef PW_CREATE_TEST_PLUGIN_WINDOW

static LRESULT CALLBACK WndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
  return DefWindowProc( hWnd, uMsg, wParam, lParam );
}

HWND DebugCreateWindow( HINSTANCE hInstance )
{
  WNDCLASS wndClass = { 0, WndProc, 0, 0, hInstance,
    NULL/*LoadIcon( hInstance, nIcon )*/,
    0, 
    (HBRUSH)GetStockObject( NULL_BRUSH ), // NULL_BRUSH // WHITE_BRUSH
    NULL, "PWPluginWindowClass" };
  wndClass.style |= CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  ATOM atomWndClassName = RegisterClass( &wndClass );
  atomWndClassName;

  DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
  DWORD dwExStyle = WS_EX_APPWINDOW;
  return CreateWindowEx( dwExStyle, "PWPluginWindowClass", "PW Window", dwStyle,
    50, 50, 800, 600,
    0, 0, hInstance, 0 );
}

#else //PW_CREATE_TEST_PLUGIN_WINDOW

HWND DebugCreateWindow( HINSTANCE hInstance ) { return 0; }

#endif //PW_CREATE_TEST_PLUGIN_WINDOW


typedef void WINAPIV TStartPWPlugin( HWND hWnd );


#ifndef DO_NOT_USE_DLLMAIN
bool LaunchGameDll()
{
  const size_t MAX_MOD_NAME = 1024;
  TCHAR szModuleName[MAX_MOD_NAME] = TEXT("");

  DWORD result = GetModuleFileName( GetModuleHandle( NULL ), szModuleName, MAX_MOD_NAME );
  if ( !result || ( result == MAX_MOD_NAME ) )
    return false;

  LPTSTR slash = StrRChr( szModuleName, NULL, TEXT( '\\' ) );
  if ( !slash )
    return false;

  slash[1] = 0;

  LPCTSTR pszDllName = TEXT( "PW_Client.dll" );

  const size_t MAX_DLL_NAME = 1536;
  TCHAR szDllName[MAX_DLL_NAME] = TEXT("");

  _ASSERT( MAX_MOD_NAME + StrLength( pszDllName ) + 1 < MAX_DLL_NAME );

  StrCpy( szDllName, szModuleName );
  StrCat( szDllName, pszDllName );

  HMODULE hDll = LoadLibraryEx( szDllName, NULL, LOAD_WITH_ALTERED_SEARCH_PATH );
  if ( !hDll )
    return false;

  TStartPWPlugin * proc = (TStartPWPlugin *)GetProcAddress( hDll, "StartPWApplication" );//TEXT( "StartPWApplication" ) );
  if ( !proc )
    return false;

  (*proc)( DebugCreateWindow( GetModuleHandle( NULL ) ) );

  FreeLibrary( hDll );
  return true;
}
#else
  extern "C" void WINAPIV StartPWApplication( HWND hWnd );
#endif


int __stdcall WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, char * lpCmdLine, int nCmdShow )
{
  AddVectoredExceptionHandler( 1, FastfailVeh );
  _set_invalid_parameter_handler( (_invalid_parameter_handler)MyInvalidParamHandler );
  _set_purecall_handler( MyPurecallHandler );
  FastfailLogLine( "handlers installed\n" );
	SetProcessDPIAware();

#ifndef DO_NOT_USE_DLLMAIN
  if ( !LaunchGameDll() )
  {
    MessageBox( 0, TEXT( "Could not launch main executable. Please re-install the application." ), TEXT( "Unpredictable Error" ), MB_OK | MB_ICONERROR );
  }
#else 
  StartPWApplication( DebugCreateWindow( GetModuleHandle( NULL ) ) );
#endif
    
  return 0;
}
