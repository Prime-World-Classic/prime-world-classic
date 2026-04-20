path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PW_Client/Game.cpp"
with open(path, "r", encoding="cp1251") as f:
    content = f.read()

# commctrl.h
content = content.replace('#include "commctrl.h"', '#ifdef _WIN32\n#include "commctrl.h"\n#endif')

# system/expreport.h and system/BSUtil.h
content = content.replace('#include "system/expreport.h"', '#include "System/expreport.h"')
content = content.replace('#include "system/BSUtil.h"', '#include "System/BSUtil.h"')

# TlHelp32.h
content = content.replace('#include <TlHelp32.h>', '#ifdef _WIN32\n#include <TlHelp32.h>\n#endif')

# _invalid_parameter_handler
content = content.replace('void _invalid_parameter_handler', '#ifdef _WIN32\nvoid _invalid_parameter_handler')
content = content.replace('g_oldInvalidParamHandler = _set_invalid_parameter_handler(DebugTraceInvalidParamsHandler);', 'g_oldInvalidParamHandler = _set_invalid_parameter_handler(DebugTraceInvalidParamsHandler);\n#endif')
content = content.replace('_set_invalid_parameter_handler(g_oldInvalidParamHandler);', '#ifdef _WIN32\n_set_invalid_parameter_handler(g_oldInvalidParamHandler);\n#endif')

# MessageBox
content = content.replace('MessageBoxW(NULL, (s_localePrefix + L" " + params[0]).c_str(), L"Locale Prefix", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);', '#ifdef _WIN32\nMessageBoxW(NULL, (s_localePrefix + L" " + params[0]).c_str(), L"Locale Prefix", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);\n#endif')

content = content.replace('if (!IsDebuggerPresent())\n  {\n    MessageBox(NULL, "Failed to create remote debugger process! Make sure DebugServer_xx.exe are built and placed to Bin folder.", "Error", MB_OK | MB_ICONINFORMATION);\n  }\n\n  InitCommonControls();', '#ifdef _WIN32\nif (!IsDebuggerPresent())\n  {\n    MessageBox(NULL, "Failed to create remote debugger process! Make sure DebugServer_xx.exe are built and placed to Bin folder.", "Error", MB_OK | MB_ICONINFORMATION);\n  }\n\n  InitCommonControls();\n#endif')

# CheckHardwareCompatibility
content = content.replace('bool CheckHardwareCompatibility()\n{', '#ifdef _WIN32\nbool CheckHardwareCompatibility()\n{')
content = content.replace('  return true;\n}\n\nvoid _invalid_parameter_handler', '  return true;\n}\n#else\nbool CheckHardwareCompatibility() { return true; }\n#endif\n\nvoid _invalid_parameter_handler')

# DumpLoadedModules
content = content.replace('void DumpLoadedModules()\n{', '#ifdef _WIN32\nvoid DumpLoadedModules()\n{')
content = content.replace('  }\n}\n\nbool CheckHardwareCompatibility', '  }\n}\n#else\nvoid DumpLoadedModules() {}\n#endif\n\nbool CheckHardwareCompatibility')

# StartRemoteDebugger
content = content.replace('void StartRemoteDebugger()\n{', '#ifdef _WIN32\nvoid StartRemoteDebugger()\n{')
content = content.replace('  CloseHandle( pi.hThread );\n}\n\nint __stdcall', '  CloseHandle( pi.hThread );\n}\n#else\nvoid StartRemoteDebugger() {}\n#endif\n\nint __stdcall')

# RaiseMainThreadPriority
content = content.replace('void RaiseMainThreadPriority()\n{', '#ifdef _WIN32\nvoid RaiseMainThreadPriority()\n{')
content = content.replace('  SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL );\n}\n\nbool ErrorMessageBox', '  SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL );\n}\n#else\nvoid RaiseMainThreadPriority() {}\n#endif\n\nbool ErrorMessageBox')

# TerminateProcess
content = content.replace('ShowWindow( hWnd, SW_MINIMIZE );\n    TerminateProcess( GetCurrentProcess(), 1 );', '#ifdef _WIN32\n    ShowWindow( hWnd, SW_MINIMIZE );\n    TerminateProcess( GetCurrentProcess(), 1 );\n#else\n    exit(1);\n#endif')

# MAKEINTRESOURCEW
content = content.replace('MAKEINTRESOURCEW(104)', '#ifdef _WIN32\nMAKEINTRESOURCEW(104)\n#else\n(const wchar_t*)104\n#endif')

# SERVER_IP_ARRAY
content = content.replace('_countof(SERVER_IP_ARRAY)', '(sizeof(SERVER_IP_ARRAY)/sizeof(SERVER_IP_ARRAY[0]))')

# CoInitialize
content = content.replace('CoInitialize(NULL);\n\n  NBSU::SystemReport sysRep;', '#ifdef _WIN32\n  CoInitialize(NULL);\n\n  NBSU::SystemReport sysRep;\n#endif')

# MapViewOfFile
content = content.replace('HANDLE hFileMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 16, TEXT("Local\\\\NivalStartLoadingFinished"));\n  if (hFileMapping)', '#ifdef _WIN32\n  HANDLE hFileMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 16, TEXT("Local\\\\NivalStartLoadingFinished"));\n  if (hFileMapping)')
content = content.replace('    }\n    UnmapViewOfFile(mappedData);\n  }', '    }\n    UnmapViewOfFile(mappedData);\n  }\n#endif')

# StartPWApplication
content = content.replace('int __stdcall StartPWApplication( void )\n{\n  return StartPWBase();\n}\n\nint __stdcall StartPWPlugin( void )\n{\n  return StartPWBase();\n}', '#ifdef _WIN32\nint __stdcall StartPWApplication( void )\n{\n  return StartPWBase();\n}\n\nint __stdcall StartPWPlugin( void )\n{\n  return StartPWBase();\n}\n#else\nint StartPWApplication( void )\n{\n  return StartPWBase();\n}\n\nint StartPWPlugin( void )\n{\n  return StartPWBase();\n}\n#endif')

# NumProcessRunning
content = content.replace('int NumProcessRunning(const char* processName)\n{', '#ifdef _WIN32\nint NumProcessRunning(const char* processName)\n{')
content = content.replace('    CloseHandle(hProcessSnap);\n    return count;\n}\n\nstatic void RunLinuxLauncher()', '    CloseHandle(hProcessSnap);\n    return count;\n}\n#else\nint NumProcessRunning(const char* processName) { return 0; }\n#endif\n\nstatic void RunLinuxLauncher()')

# RunLinuxLauncher
content = content.replace('static void RunLinuxLauncher() {\n  char curDirBuff[260];\n  GetCurrentDirectoryA(260,curDirBuff);\n\n  STARTUPINFO startupInfo;\n  PROCESS_INFORMATION processInfo;\n  ZeroMemory(&startupInfo, sizeof(startupInfo));\n  startupInfo.cb = sizeof(startupInfo);\n  ZeroMemory(&processInfo, sizeof(processInfo));\n\n  BOOL procRun = CreateProcessA("../../Launcher/PWClassic", "", NULL, NULL, FALSE, 0, NULL, NULL, &startupInfo, &processInfo);\n\n  systemLog( NLogg::LEVEL_MESSAGE ) << "Linux proc run: \\"" << procRun << "\\"" << endl;\n}', '#ifdef _WIN32\nstatic void RunLinuxLauncher() {\n  char curDirBuff[260];\n  GetCurrentDirectoryA(260,curDirBuff);\n\n  STARTUPINFO startupInfo;\n  PROCESS_INFORMATION processInfo;\n  ZeroMemory(&startupInfo, sizeof(startupInfo));\n  startupInfo.cb = sizeof(startupInfo);\n  ZeroMemory(&processInfo, sizeof(processInfo));\n\n  BOOL procRun = CreateProcessA("../../Launcher/PWClassic", "", NULL, NULL, FALSE, 0, NULL, NULL, &startupInfo, &processInfo);\n\n  systemLog( NLogg::LEVEL_MESSAGE ) << "Linux proc run: \\"" << procRun << "\\"" << endl;\n}\n#else\n#include <stdlib.h>\nstatic void RunLinuxLauncher() {\n  system("nohup ../../Launcher/PWClassic > /dev/null 2>&1 &");\n}\n#endif')

content = content.replace('reinterpret_cast<int>( (&j)+1 )', 'reinterpret_cast<intptr_t>( (&j)+1 )')

with open(path, "w", encoding="cp1251") as f:
    f.write(content)
