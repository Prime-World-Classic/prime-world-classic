// Stub windows.h for Linux build — NO type definitions
// Uses standard C types; only provides Windows-specific macros, handles, and stubs
#pragma once

#ifndef _WINDOWS_H_STUB_
#define _WINDOWS_H_STUB_

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

// ============================================================
// Platform
// ============================================================
#ifndef WIN32
#define WIN32
#endif
#ifndef _WIN32
#define _WIN32
#endif
#ifndef WINVER
#define WINVER 0x0500
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif

// ============================================================
// Calling conventions
// ============================================================
#ifndef WINAPI
#define WINAPI
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef PASCAL
#define PASCAL
#endif

// ============================================================
// MSVC extensions
// ============================================================
#ifndef __cdecl
#define __cdecl
#endif
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __declspec
#define __declspec(x)
#endif
#ifndef __forceinline
#define __forceinline inline __attribute__((always_inline))
#endif
#ifndef __inline
#define __inline inline
#endif
#ifndef _inline
#define _inline inline
#endif
#ifndef interface
#define interface struct
#endif

// ============================================================
// Windows types mapped to standard C types (no typedefs — use #define)
// ============================================================
#ifndef BOOL
#define BOOL int
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef VOID
#define VOID void
#endif

#ifndef FAR
#define FAR
#endif
#ifndef NEAR
#define NEAR
#endif
#ifndef near
#define near
#endif
#ifndef far
#define far
#endif

// ============================================================
// COM macros
// ============================================================
#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE
#endif
#ifndef STDMETHOD
#define STDMETHOD(Method) virtual HRESULT STDMETHODCALLTYPE Method
#endif
#ifndef STDMETHODIMP
#define STDMETHODIMP HRESULT STDMETHODCALLTYPE
#endif
#ifndef PURE
#define PURE = 0
#endif
#ifndef THIS_
#define THIS_
#endif
#ifndef THIS_CALL
#define THIS_CALL
#endif

// ============================================================
// Handle types
// ============================================================
#ifndef DECLARE_HANDLE
#define DECLARE_HANDLE(name) struct name##__ { int unused; }; typedef struct name##__ *name
#endif

DECLARE_HANDLE(HANDLE);
DECLARE_HANDLE(HMODULE);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HCURSOR);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HGDIOBJ);
DECLARE_HANDLE(HMONITOR);
DECLARE_HANDLE(HFILE);
DECLARE_HANDLE(HGLOBAL);
DECLARE_HANDLE(HLOCAL);
DECLARE_HANDLE(HRGN);
DECLARE_HANDLE(HWND);

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#endif

// ============================================================
// Memory macros
// ============================================================
#define CopyMemory(Dest, Source, Size) memcpy((Dest), (Source), (Size))
#define FillMemory(Dest, Size, Fill) memset((Dest), (Fill), (Size))
#define ZeroMemory(Dest, Size) memset((Dest), 0, (Size))
#define MoveMemory(Dest, Source, Size) memmove((Dest), (Source), (Size))

// ============================================================
// Utility macros
// ============================================================
#define LOBYTE(w) ((unsigned char)((unsigned short)(w)))
#define HIBYTE(w) ((unsigned char)(((unsigned short)(w) >> 8) & 0xFF))
#define LOWORD(l) ((unsigned short)((unsigned long)(l)))
#define HIWORD(l) ((unsigned short)(((unsigned long)(l) >> 16) & 0xFFFF))
#define MAKEWORD(a, b) ((unsigned short)(((unsigned char)(a)) | ((unsigned short)((unsigned char)(b)) << 8)))
#define MAKELONG(a, b) ((long)(((unsigned short)(a)) | ((unsigned long)((unsigned short)(b)) << 16)))
#define FIELD_OFFSET(type, field) ((long)(ptrdiff_t)&(((type *)0)->field))
#define CONTAINING_RECORD(address, type, field) ((type *)((char *)(address) - FIELD_OFFSET(type, field)))
#define INITGUID

// SAL annotations
#ifndef __in
#define __in
#endif
#ifndef __out
#define __out
#endif
#ifndef __inout
#define __inout
#endif
#ifndef __in_opt
#define __in_opt
#endif
#ifndef __out_opt
#define __out_opt
#endif

// ============================================================
// Exception handling stubs (Windows SEH)
// ============================================================
#ifndef EXCEPTION_EXECUTE_HANDLER
#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION -1
#endif
#ifndef __try
#define __try if (0) { } else
#endif
#ifndef __except
#define __except(expr)
#endif
#ifndef __leave
#define __leave return
#endif

// ============================================================
// String macros
// ============================================================
#ifndef _T
#define _T(x) x
#endif
#ifndef __T
#define __T(x) x
#endif
#ifndef lstrlenA
#define lstrlenA(s) strlen(s)
#define lstrlenW(s) wcslen(s)
#define lstrlen(s) strlen(s)
#define lstrcmpA(s1, s2) strcmp((s1), (s2))
#define lstrcmpW(s1, s2) wcscmp((s1), (s2))
#define lstrcmp(s1, s2) strcmp((s1), (s2))
#define lstrcmpiA(s1, s2) strcasecmp((s1), (s2))
#define lstrcmpiW(s1, s2) wcscasecmp((s1), (s2))
#define lstrcmpi(s1, s2) strcasecmp((s1), (s2))
#endif

// ============================================================
// Function stubs (weak)
// ============================================================
#ifdef __cplusplus
extern "C" {
#endif
    __attribute__((weak)) void* LoadLibraryA(const char *) { return NULL; }
    __attribute__((weak)) void* LoadLibraryW(const wchar_t *) { return NULL; }
    __attribute__((weak)) int FreeLibrary(void *) { return 1; }
    __attribute__((weak)) void* GetModuleHandleA(const char *) { return NULL; }
    __attribute__((weak)) void* GetProcAddress(void *, const char *) { return NULL; }
    __attribute__((weak)) void* VirtualAlloc(void *, size_t, unsigned int, unsigned int) { return NULL; }
    __attribute__((weak)) int VirtualFree(void *, size_t, unsigned int) { return 1; }
    __attribute__((weak)) unsigned int GetLastError(void) { return 0; }
    __attribute__((weak)) void SetLastError(unsigned int) {}
#ifdef __cplusplus
}
#endif

#define MEM_COMMIT  0x1000
#define MEM_RESERVE 0x2000
#define MEM_RELEASE 0x8000
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_EXECUTE_READWRITE 0x40

#define OutputDebugStringA(s)
#define OutputDebugStringW(s)
#define OutputDebugString(s)
#ifndef VERIFY
#define VERIFY(x) (x)
#endif

#endif // _WINDOWS_H_STUB_
