import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Win32_linux.h"
with open(path, "r") as f:
    text = f.read()

stubs = """
#ifndef _WIN32_LINUX_EXTRA_STUBS
#define _WIN32_LINUX_EXTRA_STUBS

#include <stdint.h>

// Toolhelp32
#define TH32CS_SNAPMODULE 0x00000008
typedef struct tagMODULEENTRY32 {
    DWORD   dwSize;
    DWORD   th32ModuleID;
    DWORD   th32ProcessID;
    DWORD   GlblcntUsage;
    DWORD   ProccntUsage;
    BYTE  * modBaseAddr;
    DWORD   modBaseSize;
    HMODULE hModule;
    char    szModule[256];
    char    szExePath[260];
} MODULEENTRY32, *PMODULEENTRY32, *LPMODULEENTRY32;

inline HANDLE CreateToolhelp32Snapshot(DWORD dwFlags, DWORD th32ProcessID) { return INVALID_HANDLE_VALUE; }
inline BOOL Module32First(HANDLE hSnapshot, LPMODULEENTRY32 lpme) { return FALSE; }
inline BOOL Module32Next(HANDLE hSnapshot, LPMODULEENTRY32 lpme) { return FALSE; }

// Memory
typedef struct _MEMORYSTATUSEX {
    DWORD     dwLength;
    DWORD     dwMemoryLoad;
    uint64_t  ullTotalPhys;
    uint64_t  ullAvailPhys;
    uint64_t  ullTotalPageFile;
    uint64_t  ullAvailPageFile;
    uint64_t  ullTotalVirtual;
    uint64_t  ullAvailVirtual;
    uint64_t  ullAvailExtendedVirtual;
} MEMORYSTATUSEX, *LPMEMORYSTATUSEX;

inline BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX lpBuffer) {
    if (lpBuffer) {
        lpBuffer->ullTotalPhys = 4ULL * 1024 * 1024 * 1024; // 4GB dummy
        return TRUE;
    }
    return FALSE;
}

// Invalid param handler
typedef void (*_invalid_parameter_handler)(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t);
inline _invalid_parameter_handler _set_invalid_parameter_handler(_invalid_parameter_handler pNew) { return 0; }

// Process
typedef struct _STARTUPINFOA {
    DWORD  cb;
    LPSTR  lpReserved;
    LPSTR  lpDesktop;
    LPSTR  lpTitle;
    DWORD  dwX;
    DWORD  dwY;
    DWORD  dwXSize;
    DWORD  dwYSize;
    DWORD  dwXCountChars;
    DWORD  dwYCountChars;
    DWORD  dwFillAttribute;
    DWORD  dwFlags;
    WORD   wShowWindow;
    WORD   cbReserved2;
    LPBYTE lpReserved2;
    HANDLE hStdInput;
    HANDLE hStdOutput;
    HANDLE hStdError;
} STARTUPINFOA, *LPSTARTUPINFOA;
typedef STARTUPINFOA STARTUPINFO;

typedef struct _PROCESS_INFORMATION {
    HANDLE hProcess;
    HANDLE hThread;
    DWORD  dwProcessId;
    DWORD  dwThreadId;
} PROCESS_INFORMATION, *PPROCESS_INFORMATION, *LPPROCESS_INFORMATION;

inline BOOL CreateProcessA(LPCSTR lpApplicationName, LPSTR lpCommandLine, void* lpProcessAttributes, void* lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCSTR lpCurrentDirectory, LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation) { return FALSE; }
inline DWORD ExpandEnvironmentStrings(LPCSTR lpSrc, LPSTR lpDst, DWORD nSize) { return 0; }
inline DWORD GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer) { return 0; }
inline DWORD GetModuleFileName(HMODULE hModule, LPSTR lpFilename, DWORD nSize) { 
    if (lpFilename && nSize > 0) { lpFilename[0] = 0; }
    return 0; 
}

// Thread
#define THREAD_PRIORITY_ABOVE_NORMAL 1
inline HANDLE GetCurrentThread() { return (HANDLE)1; }
inline BOOL SetThreadIdealProcessor(HANDLE hThread, DWORD dwIdealProcessor) { return TRUE; }
inline int GetThreadPriority(HANDLE hThread) { return 0; }
inline BOOL SetThreadPriority(HANDLE hThread, int nPriority) { return TRUE; }

// UI
#define SW_MINIMIZE 6
#define MB_TOPMOST 0x00040000L
#define MB_SETFOREGROUND 0x00010000L
inline BOOL ShowWindow(HWND hWnd, int nCmdShow) { return TRUE; }
inline void InitCommonControls() {}
#define MAKEINTRESOURCEW(i) ((const wchar_t*)((uintptr_t)((WORD)(i))))

// File Mapping
#define PAGE_READWRITE 0x04
#define FILE_MAP_WRITE 2
inline HANDLE CreateFileMapping(HANDLE hFile, void* lpFileMappingAttributes, DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCSTR lpName) { return NULL; }
inline LPVOID MapViewOfFile(HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap) { return NULL; }
inline BOOL UnmapViewOfFile(LPCVOID lpBaseAddress) { return TRUE; }

// COM
inline HRESULT CoInitialize(LPVOID pvReserved) { return 0; }

// Misc
#define IsDebuggerPresent NiIsDebuggerPresent
#define _BitScanForward(Index, Mask) (0)

#endif // _WIN32_LINUX_EXTRA_STUBS
"""

if "_WIN32_LINUX_EXTRA_STUBS" not in text:
    text = text.replace("#endif // __WIN32_LINUX_H_UNIQUE__", stubs + "\n#endif // __WIN32_LINUX_H_UNIQUE__")
    with open(path, "w") as f:
        f.write(text)
        print("Patched Win32_linux.h")
