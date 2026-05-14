path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PW_Client/Game.cpp"
with open(path, "r", encoding="cp1251") as f:
    content = f.read()

# Remove outermost #ifdef _WIN32 and #endif
content = content.replace('#include "stdafx.h"\n#ifdef _WIN32\n', '#include "stdafx.h"\n#if 1\n')

# Dummy PseudoWinMain at the end
content = content.replace('''REGISTER_CMD( debug_crash_now, DebugCrashNow );
REGISTER_CMD( malloc_mask, SetMallocThreadMask )
#else
#include <stdlib.h>
#include <stdio.h>
#include <SDL2/SDL.h>
extern int __stdcall PseudoWinMain( void* hInstance, void* hWnd, char* lpCmdLine, void* pluginSett );
extern "C" {
void StartPWApplication(void* hWnd) {
  printf("==================================================\\n");
  printf(" Prime World Linux Native Client (SDL2)\\n");
  printf("==================================================\\n");

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
      printf("Failed to initialize SDL2: %s\\n", SDL_GetError());
      _exit(1);
  }

  SDL_Window* win = SDL_CreateWindow("Prime World Native Linux Client", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 768, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  if (!win) {
      printf("Failed to create SDL2 window: %s\\n", SDL_GetError());
      _exit(1);
  }
  printf("SDL2 Window created successfully. Booting game engine...\\n");

  PseudoWinMain( 0, (void*)win, (char*)"", 0 );

  SDL_DestroyWindow(win);
  SDL_Quit();
  _exit(0);
}
void StartPWPlugin(void* hWnd, int width, int height, bool fullscreen, const char* sessionLogin) {}
}

int __stdcall PseudoWinMain( void* hInstance, void* hWnd, char* lpCmdLine, void* pluginSett ) {
  printf("Inside PseudoWinMain dummy.\\n");
  return 0;
}
#endif''', '''REGISTER_CMD( debug_crash_now, DebugCrashNow );
REGISTER_CMD( malloc_mask, SetMallocThreadMask )
#endif

#ifndef _WIN32
#include <stdlib.h>
#include <stdio.h>
#include <SDL2/SDL.h>
extern int __stdcall PseudoWinMain( HINSTANCE hInstance, HWND hWnd, LPTSTR lpCmdLine, SPluginSettings * pluginSett );
extern "C" {
void StartPWApplication(void* hWnd) {
  printf("==================================================\\n");
  printf(" Prime World Linux Native Client (SDL2)\\n");
  printf("==================================================\\n");

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
      printf("Failed to initialize SDL2: %s\\n", SDL_GetError());
      _exit(1);
  }

  SDL_Window* win = SDL_CreateWindow("Prime World Native Linux Client", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 768, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  if (!win) {
      printf("Failed to create SDL2 window: %s\\n", SDL_GetError());
      _exit(1);
  }
  printf("SDL2 Window created successfully. Booting game engine...\\n");

  PseudoWinMain( 0, (HWND)win, (LPTSTR)"", 0 );

  SDL_DestroyWindow(win);
  SDL_Quit();
  _exit(0);
}
void StartPWPlugin(void* hWnd, int width, int height, bool fullscreen, const char* sessionLogin) {}
}
#endif
''')

# _invalid_parameter_handler and DebugTraceInvalidParamsHandler
content = content.replace('void DebugTraceInvalidParamsHandler', '#ifdef _WIN32\nvoid DebugTraceInvalidParamsHandler')
content = content.replace('g_oldInvalidParamHandler = _set_invalid_parameter_handler( DebugTraceInvalidParamsHandler );', 'g_oldInvalidParamHandler = _set_invalid_parameter_handler( DebugTraceInvalidParamsHandler );\n#endif')
content = content.replace('_set_invalid_parameter_handler( g_oldInvalidParamHandler );', '#ifdef _WIN32\n_set_invalid_parameter_handler( g_oldInvalidParamHandler );\n#endif')

# IsDebuggerPresent and MessageBox, InitCommonControls
content = content.replace('if (!IsDebuggerPresent())', '#ifdef _WIN32\nif (!IsDebuggerPresent())')
content = content.replace('InitCommonControls();', 'InitCommonControls();\n#endif')

# StartRemoteDebugger
content = content.replace('bool StartRemoteDebugger()\n{', '#ifdef _WIN32\nbool StartRemoteDebugger()\n{')
content = content.replace('CloseHandle( pi.hThread );\n  return true;\n}', 'CloseHandle( pi.hThread );\n  return true;\n}\n#else\nbool StartRemoteDebugger() { return false; }\n#endif')

# RaiseMainThreadPriority
content = content.replace('void RaiseMainThreadPriority()\n{', '#ifdef _WIN32\nvoid RaiseMainThreadPriority()\n{')
content = content.replace('SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);\n      }\n    }\n  }\n}', 'SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);\n      }\n    }\n  }\n}\n#else\nvoid RaiseMainThreadPriority() {}\n#endif')

# MessageBoxW in HandleMessage
content = content.replace('MessageBoxW(NULL, (s_localePrefix + L" " + params[0]).c_str(), L"Locale Prefix", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);', '#ifdef _WIN32\nMessageBoxW(NULL, (s_localePrefix + L" " + params[0]).c_str(), L"Locale Prefix", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);\n#endif')

# DumpLoadedModules
content = content.replace('void DumpLoadedModules()\n{', '#ifdef _WIN32\nvoid DumpLoadedModules()\n{')
content = content.replace('      OutputDebugString( msg.c_str() );\n    }\n  }\n}', '      OutputDebugString( msg.c_str() );\n    }\n  }\n}\n#else\nvoid DumpLoadedModules() {}\n#endif')

# CheckHardwareCompatibility
content = content.replace('bool CheckHardwareCompatibility()\n{', '#ifdef _WIN32\nbool CheckHardwareCompatibility()\n{')
content = content.replace('          return false;\n        }\n      }\n    }\n  }\n  return true;\n}', '          return false;\n        }\n      }\n    }\n  }\n  return true;\n}\n#else\nbool CheckHardwareCompatibility() { return true; }\n#endif')

# ShowWindow and TerminateProcess in HandleMessage/Crash
content = content.replace('ShowWindow(hWnd, SW_MINIMIZE);\n        TerminateProcess(GetCurrentProcess(), 1);', '#ifdef _WIN32\nShowWindow(hWnd, SW_MINIMIZE);\n        TerminateProcess(GetCurrentProcess(), 1);\n#else\nexit(1);\n#endif')
content = content.replace('ShowWindow( hWnd, SW_MINIMIZE );\n    TerminateProcess( GetCurrentProcess(), 1 );', '#ifdef _WIN32\nShowWindow( hWnd, SW_MINIMIZE );\n    TerminateProcess( GetCurrentProcess(), 1 );\n#else\nexit(1);\n#endif')

# StartPWApplication and StartPWPlugin (the real ones)
content = content.replace('INTERMODULE_EXPORT void WINAPIV StartPWApplication( HWND hWnd )\n{', '#ifdef _WIN32\nINTERMODULE_EXPORT void WINAPIV StartPWApplication( HWND hWnd )\n{')
content = content.replace('TerminateProcess( GetCurrentProcess(), 0 );\n}', 'TerminateProcess( GetCurrentProcess(), 0 );\n}\n#endif')

# CreateFileMapping
content = content.replace('HANDLE hFileMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(RECT), strFileMapName);', '#ifdef _WIN32\nHANDLE hFileMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(RECT), strFileMapName);')
content = content.replace('UnmapViewOfFile(pRect);\n          }\n        }\n      }\n    }', 'UnmapViewOfFile(pRect);\n          }\n        }\n      }\n    }\n#endif')

# CoInitialize and NBSU::SystemReport
content = content.replace('CoInitialize(NULL);\n\n    NBSU::SystemReport sysRep;', '#ifdef _WIN32\nCoInitialize(NULL);\n\n    NBSU::SystemReport sysRep;')
content = content.replace('sysRep.dumpSystemInfo(true);\n  }', 'sysRep.dumpSystemInfo(true);\n  }\n#endif')

# MAKEINTRESOURCEW
content = content.replace('MAKEINTRESOURCEW( IDI_MAIN )', '((const wchar_t*)104)')

# RunLinuxLauncher
content = content.replace('void RunLinuxLauncher()\n{', '#ifdef _WIN32\nvoid RunLinuxLauncher()\n{')
content = content.replace('systemLog( NLogg::LEVEL_MESSAGE ) << "Linux proc run: \\"" << procRun << "\\"" << endl;\n}', 'systemLog( NLogg::LEVEL_MESSAGE ) << "Linux proc run: \\"" << procRun << "\\"" << endl;\n}\n#else\nvoid RunLinuxLauncher() { system("nohup ../../Launcher/PWClassic > /dev/null 2>&1 &"); }\n#endif')

# NumProcessRunning
content = content.replace('int NumProcessRunning(const char* processName)\n{', '#ifdef _WIN32\nint NumProcessRunning(const char* processName)\n{')
content = content.replace('CloseHandle(hProcessSnap);\n    return count;\n}', 'CloseHandle(hProcessSnap);\n    return count;\n}\n#else\nint NumProcessRunning(const char* processName) { return 0; }\n#endif')

# Sleep ambiguous call
content = content.replace('::Sleep(5000);', 'threading::Sleep(5000);')
content = content.replace('Sleep( g_inactiveSleep );', 'threading::Sleep( g_inactiveSleep );')

# GetModuleFileName
content = content.replace('char buffer[MAX_PATH];\n    GetModuleFileName(NULL, buffer, MAX_PATH);', '#ifdef _WIN32\nchar buffer[MAX_PATH];\n    GetModuleFileName(NULL, buffer, MAX_PATH);\n#else\nchar buffer[260] = "PrimeWorld";\n#endif')

# Tlhelp32.h
content = content.replace('#include <Tlhelp32.h>', '#ifdef _WIN32\n#include <Tlhelp32.h>\n#endif')

with open(path, "w", encoding="cp1251") as f:
    f.write(content)
