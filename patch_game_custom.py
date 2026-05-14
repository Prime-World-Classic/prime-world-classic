import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PW_Client/Game.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

# Remove outermost #ifdef _WIN32 and #endif
text = re.sub(r'^#include "stdafx\.h"\n#ifdef _WIN32\n', '#include "stdafx.h"\n#if 1\n', text, count=1)

# Remove the dummy PseudoWinMain and fix the #else block
text = re.sub(r'REGISTER_CMD\( malloc_mask, SetMallocThreadMask \)\n#else\n#include <stdlib\.h>[\s\S]*?#endif\n?$', '''REGISTER_CMD( malloc_mask, SetMallocThreadMask )
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
''', text)

# _invalid_parameter_handler and DebugTraceInvalidParamsHandler
text = re.sub(
    r'(void DebugTraceInvalidParamsHandler[\s\S]*?_set_invalid_parameter_handler\(\s*DebugTraceInvalidParamsHandler\s*\);)',
    r'#ifdef _WIN32\n\1\n#endif', text)
text = re.sub(
    r'(_set_invalid_parameter_handler\(\s*g_oldInvalidParamHandler\s*\);)',
    r'#ifdef _WIN32\n\1\n#endif', text)

# IsDebuggerPresent and MessageBox, InitCommonControls
text = re.sub(
    r'(if \(!IsDebuggerPresent\(\)\)[\s\S]*?InitCommonControls\(\);)',
    r'#ifdef _WIN32\n\1\n#endif', text)

# StartRemoteDebugger
text = re.sub(
    r'(bool StartRemoteDebugger\(\)\n\{[\s\S]*?\n\})',
    r'#ifdef _WIN32\n\1\n#else\nbool StartRemoteDebugger() { return false; }\n#endif', text)

# RaiseMainThreadPriority
text = re.sub(
    r'(void RaiseMainThreadPriority\(\)\n\{[\s\S]*?\n\})',
    r'#ifdef _WIN32\n\1\n#else\nvoid RaiseMainThreadPriority() {}\n#endif', text)

# MessageBoxW in HandleMessage
text = re.sub(
    r'(MessageBoxW\(NULL, [^\)]+MB_TOPMOST\);)',
    r'#ifdef _WIN32\n\1\n#endif', text, flags=re.DOTALL)

# DumpLoadedModules
text = re.sub(
    r'(void DumpLoadedModules\(\)\n\{[\s\S]*?\n\})',
    r'#ifdef _WIN32\n\1\n#else\nvoid DumpLoadedModules() {}\n#endif', text)

# CheckHardwareCompatibility
text = re.sub(
    r'(bool CheckHardwareCompatibility\(\)\n\{[\s\S]*?\n\})',
    r'#ifdef _WIN32\n\1\n#else\nbool CheckHardwareCompatibility() { return true; }\n#endif', text)

# ShowWindow and TerminateProcess in HandleMessage/Crash
text = re.sub(
    r'(ShowWindow\(\s*hWnd,\s*SW_MINIMIZE\s*\);\n\s*TerminateProcess\(\s*GetCurrentProcess\(\),\s*1\s*\);)',
    r'#ifdef _WIN32\n\1\n#else\nexit(1);\n#endif', text)

text = re.sub(
    r'(ShowWindow\(\s*hWnd,\s*SW_MINIMIZE\s*\);)',
    r'#ifdef _WIN32\n\1\n#endif', text)

# StartPWApplication and StartPWPlugin (the real ones)
text = re.sub(
    r'(INTERMODULE_EXPORT void WINAPIV StartPWApplication\( HWND hWnd \)\n\{\n\s*PseudoWinMain\( GetModuleHandle\( NULL \), hWnd, GetCommandLine\(\), 0 \);\n\}\n\nINTERMODULE_EXPORT void WINAPIV StartPWPlugin\( HWND hWnd, int width, int height, bool fullscreen, const char \* sessionLogin \)\n\{[\s\S]*?TerminateProcess\( GetCurrentProcess\(\), 0 \);\n\})',
    r'#ifdef _WIN32\n\1\n#endif', text)

# CreateFileMapping
text = re.sub(
    r'(HANDLE hFileMapping = CreateFileMapping[\s\S]*?UnmapViewOfFile\(\s*pRect\s*\);\n\s*\})',
    r'#ifdef _WIN32\n\1\n#endif', text)

# CoInitialize and NBSU::SystemReport
text = re.sub(
    r'(CoInitialize\(NULL\);\n\n\s*NBSU::SystemReport sysRep;[\s\S]*?sysRep\.dumpSystemInfo\(true\);)',
    r'#ifdef _WIN32\n\1\n#endif', text)

# MAKEINTRESOURCEW
text = re.sub(
    r'(MAKEINTRESOURCEW\( IDI_MAIN \))',
    r'#ifdef _WIN32\n\1\n#else\n(const wchar_t*)104\n#endif', text)

# RunLinuxLauncher
text = re.sub(
    r'(void RunLinuxLauncher\(\)\n\{\n\s*char curDirBuff\[260\];[\s\S]*?systemLog\( NLogg::LEVEL_MESSAGE \) << "Linux proc run: \\"" << procRun << "\\"" << endl;\n\})',
    r'#ifdef _WIN32\n\1\n#else\nvoid RunLinuxLauncher() { system("nohup ../../Launcher/PWClassic > /dev/null 2>&1 &"); }\n#endif', text)

# NumProcessRunning
text = re.sub(
    r'(int NumProcessRunning\(const char\* processName\)\n\{[\s\S]*?return count;\n\})',
    r'#ifdef _WIN32\n\1\n#else\nint NumProcessRunning(const char* processName) { return 0; }\n#endif', text)

# Sleep ambiguous call
text = re.sub(
    r'([^\w:])(Sleep\(\s*\w+\s*\);)',
    r'\1#ifdef _WIN32\n\2\n#else\nthreading::\2\n#endif', text)
text = re.sub(
    r'(::Sleep\(\s*\w+\s*\);)',
    r'#ifdef _WIN32\n\1\n#else\nthreading::Sleep(5000);\n#endif', text)

# GetModuleFileName
text = re.sub(
    r'(char buffer\[MAX_PATH\];\n\s*GetModuleFileName\(NULL, buffer, MAX_PATH\);)',
    r'#ifdef _WIN32\n\1\n#else\nchar buffer[260] = "PrimeWorld";\n#endif', text)

with open(path, "w", encoding="cp1251") as f:
    f.write(text)
