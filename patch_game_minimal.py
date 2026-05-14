path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PW_Client/Game.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

# 1. Change first #ifdef _WIN32 to #if 1
text = text.replace('#include "stdafx.h"\n#ifdef _WIN32\n', '#include "stdafx.h"\n#if 1\n')

# 2. Fix dummy PseudoWinMain and insert SDL code
text = text.replace('''REGISTER_CMD( debug_crash_now, DebugCrashNow );
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
#endif // end of #if 1 from beginning of file

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

# 3. Disable the original StartPWApplication/Plugin for Linux
text = text.replace('''INTERMODULE_EXPORT void WINAPIV StartPWApplication( HWND hWnd )
{
  PseudoWinMain( GetModuleHandle( NULL ), hWnd, GetCommandLine(), 0 );
}

INTERMODULE_EXPORT void WINAPIV StartPWPlugin( HWND hWnd, int width, int height, bool fullscreen, const char * sessionLogin )
{
  SPluginSettings sett;
  sett.width = width;
  sett.height = height;
  sett.fullscreen = fullscreen;
  sett.sessionLogin = sessionLogin;
  PseudoWinMain( GetModuleHandle( NULL ), hWnd, GetCommandLine(), &sett );

  TerminateProcess( GetCurrentProcess(), 0 );
}''', '''#ifdef _WIN32
INTERMODULE_EXPORT void WINAPIV StartPWApplication( HWND hWnd )
{
  PseudoWinMain( GetModuleHandle( NULL ), hWnd, GetCommandLine(), 0 );
}

INTERMODULE_EXPORT void WINAPIV StartPWPlugin( HWND hWnd, int width, int height, bool fullscreen, const char * sessionLogin )
{
  SPluginSettings sett;
  sett.width = width;
  sett.height = height;
  sett.fullscreen = fullscreen;
  sett.sessionLogin = sessionLogin;
  PseudoWinMain( GetModuleHandle( NULL ), hWnd, GetCommandLine(), &sett );

  TerminateProcess( GetCurrentProcess(), 0 );
}
#endif''')

# 4. Sleep needs prefixing or skipping due to ambiguity
text = text.replace('::Sleep(5000);', 'threading::Sleep(5000);')
text = text.replace('Sleep( g_inactiveSleep );', 'threading::Sleep( g_inactiveSleep );')
text = text.replace('Sleep( 10 );', 'threading::Sleep( 10 );')

with open(path, "w", encoding="cp1251") as f:
    f.write(text)
print("Patched Game.cpp")
