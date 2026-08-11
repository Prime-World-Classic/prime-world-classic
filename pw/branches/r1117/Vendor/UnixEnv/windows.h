// Stub windows.h for Linux (minimal — only what DirectX headers need)
#pragma once

#ifndef _WINDOWS_H_STUB_
#define _WINDOWS_H_STUB_

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

// ============================================================
// Platform detection
// ============================================================
#ifndef WIN32
#define WIN32
#endif

#ifndef _WIN32
#define _WIN32
#endif

#ifndef _WIN64
#define _WIN64
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
#define APIENTRY WINAPI
#endif

#ifndef APIPRIVATE
#define APIPRIVATE
#endif

#ifndef PASCAL
#define PASCAL
#endif

// ============================================================
// Basic integer types
// ============================================================
#ifndef _WCHAR_T_DEFINED
typedef wchar_t WCHAR;
#define _WCHAR_T_DEFINED
#endif

#ifndef _BOOLEAN_DEFINED
typedef unsigned char BOOLEAN;
#define _BOOLEAN_DEFINED
#endif

#ifndef _BYTE_DEFINED
typedef uint8_t BYTE;
#define _BYTE_DEFINED
#endif

#ifndef _USHORT_DEFINED
typedef uint16_t USHORT;
#define _USHORT_DEFINED
#endif

#ifndef _WORD_DEFINED
typedef uint16_t WORD;
#define _WORD_DEFINED
#endif

#ifndef _UINT_DEFINED
typedef uint32_t UINT;
#define _UINT_DEFINED
#endif

#ifndef _ULONG_DEFINED
typedef uint32_t ULONG;
#define _ULONG_DEFINED
#endif

#ifndef _DWORD_DEFINED
typedef uint32_t DWORD;
#define _DWORD_DEFINED
#endif

#ifndef _LONG_DEFINED
typedef int32_t LONG;
#define _LONG_DEFINED
#endif

#ifndef _INT8_DEFINED
typedef int8_t INT8;
#define _INT8_DEFINED
#endif

#ifndef _UINT8_DEFINED
typedef uint8_t UINT8;
#define _UINT8_DEFINED
#endif

#ifndef _INT16_DEFINED
typedef int16_t INT16;
#define _INT16_DEFINED
#endif

#ifndef _UINT16_DEFINED
typedef uint16_t UINT16;
#define _UINT16_DEFINED
#endif

#ifndef _INT32_DEFINED
typedef int32_t INT32;
#define _INT32_DEFINED
#endif

#ifndef _UINT32_DEFINED
typedef uint32_t UINT32;
#define _UINT32_DEFINED
#endif

#ifndef _INT64_DEFINED
typedef int64_t INT64;
#define _INT64_DEFINED
#endif

#ifndef _UINT64_DEFINED
typedef uint64_t UINT64;
#define _UINT64_DEFINED
#endif

// ============================================================
// BOOL
// ============================================================
#ifndef _BOOL_DEFINED
typedef int BOOL;
#define _BOOL_DEFINED
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

// ============================================================
// Pointer types
// ============================================================
typedef void *LPVOID;
typedef const void *LPCVOID;

// ============================================================
// String types
// ============================================================
typedef char CHAR;
typedef CHAR *LPSTR;
typedef const char *LPCSTR;

typedef WCHAR *LPWSTR;
typedef const WCHAR *LPCWSTR;

// On Linux we use ANSI strings (no UNICODE define)
#ifndef UNICODE
#define _T(x) x
#define __T(x) x
typedef CHAR TCHAR;
typedef LPSTR LPTCH;
typedef LPSTR PTCH;
typedef LPSTR PTSTR;
typedef LPSTR LPTSTR;
typedef LPCSTR LPCTSTR;
typedef LPCSTR LPCTSTR;
typedef LPCSTR LP_C8RT_STRING;
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

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#endif

// ============================================================
// COM types (also in objbase.h but declared here too)
// ============================================================
#ifndef _HRESULT_DEFINED
#define _HRESULT_DEFINED
typedef unsigned long HRESULT;
#endif

#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE
#endif

#ifndef STDMETHOD
#define STDMETHOD(Method) virtual HRESULT STDMETHODCALLTYPE Method
#endif

#ifndef STDMETHODIMP
#define STDMETHODIMP HRESULT STDMETHODCALLTYPE
#endif

#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE
#endif

#ifndef PURE
#define PURE = 0
#endif

// ============================================================
// Common error codes
// ============================================================
#ifndef NO_ERROR
#define NO_ERROR 0L
#endif

#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0L
#endif

// ============================================================
// Memory functions (map to standard C)
// ============================================================
#define CopyMemory(Dest, Source, Size) memcpy((Dest), (Source), (Size))
#define FillMemory(Dest, Size, Fill) memset((Dest), (Fill), (Size))
#define SetMemory(Dest, Size, Fill) memset((Dest), (Fill), (Size))
#define ZeroMemory(Dest, Size) memset((Dest), 0, (Size))
#define SecureZeroMemory(Dest, Size) memset_s((Dest), (Size), 0, (Size))
#define MoveMemory(Dest, Source, Size) memmove((Dest), (Source), (Size))
#define CompareMemory(Mem1, Mem2, Length) memcmp((Mem1), (Mem2), (Length))

// ============================================================
// String functions
// ============================================================
#define lstrcpyn(Dest, Source, Size) strncpy_s((Dest), (Size), (Source), _TRUNCATE)
#define lstrlenA(s) strlen(s)
#define lstrlenW(s) wcslen(s)
#define lstrlen(s) lstrlenA(s)
#define lstrcmpA(s1, s2) strcmp((s1), (s2))
#define lstrcmpW(s1, s2) wcscmp((s1), (s2))
#define lstrcmp(s1, s2) lstrcmpA((s1), (s2))
#define lstrcmpiA(s1, s2) strcasecmp((s1), (s2))
#define lstrcmpiW(s1, s2) wcscasecmp((s1), (s2))
#define lstrcmpi(s1, s2) lstrcmpiA((s1), (s2))
#define lstrcpyA(Dest, Source) strcpy((Dest), (Source))
#define lstrcpyW(Dest, Source) wcscpy((Dest), (Source))
#define lstrcpy(Dest, Source) lstrcpyA((Dest), (Source))
#define lstrcatA(Dest, Source) strcat((Dest), (Source))
#define lstrcatW(Dest, Source) wcscat((Dest), (Source))
#define lstrcat(Dest, Source) lstrcatA((Dest), (Source))

// ============================================================
// Various macros
// ============================================================
#define FAR
#define NEAR
#define near
#define far
#define PAGED_CODE()
#define LOBYTE(w) ((BYTE)((WORD)(w)))
#define HIBYTE(w) ((BYTE)(((WORD)(w) >> 8) & 0xFF))
#define LOWORD(l) ((WORD)((DWORD)(l)))
#define HIWORD(l) ((WORD)(((DWORD)(l) >> 16) & 0xFFFF))
#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b)) << 8)))
#define MAKELONG(a, b) ((LONG)(((WORD)(a)) | ((DWORD)((WORD)(b)) << 16)))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define FIELD_OFFSET(type, field) ((LONG)(ptrdiff_t)&(((type *)0)->field))
#define CONTAINING_RECORD(address, type, field) ((type *)((CHAR *)(address) - FIELD_OFFSET(type, field)))
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    EXTERN_C const GUID name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }
#define INITGUID

// ============================================================
// Rect structure
// ============================================================
typedef struct _RECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *LPRECT;

// ============================================================
// POINT structures
// ============================================================
typedef struct tagPOINT {
    LONG x;
    LONG y;
} POINT, *PPOINT, *LPPOINT;

typedef struct tagPOINTFLOAT {
    FLOAT x;
    FLOAT y;
} POINTFLOAT, *PPOINTFLOAT;

typedef struct tagPOINTL {
    LONG x;
    LONG y;
} POINTL, *PPOINTL;

// ============================================================
// SIZE structure
// ============================================================
typedef struct tagSIZE {
    LONG cx;
    LONG cy;
} SIZE, *PSIZE, *LPSIZE;

// ============================================================
// COLORREF
// ============================================================
typedef DWORD COLORREF;

// ============================================================
// VOID
// ============================================================
#ifndef VOID
#define VOID void
#endif
typedef void VOID;

// ============================================================
// Floating point
// ============================================================
#ifndef FLOAT_DEFINED
typedef float FLOAT;
#define FLOAT_DEFINED
#endif

// ============================================================
// Function stubs
// ============================================================
#ifdef __cplusplus
extern "C" {
#endif

    __attribute__((weak)) void* LoadLibraryA(const char *lpFileName) { return NULL; }
    __attribute__((weak)) void* LoadLibraryW(const wchar_t *lpFileName) { return NULL; }
    __attribute__((weak)) BOOL FreeLibrary(HMODULE hModule) { return TRUE; }
    __attribute__((weak)) void* GetModuleHandleA(const char *lpModuleName) { return NULL; }
    __attribute__((weak)) void* GetProcAddress(HMODULE hModule, const char *lpProcName) { return NULL; }
    __attribute__((weak)) void* VirtualAlloc(void *lpAddress, SIZE_T dwSize, DWORD flAllocateType, DWORD flProtect) { return NULL; }
    __attribute__((weak)) BOOL VirtualFree(void *lpAddress, SIZE_T dwSize, DWORD dwFreeType) { return TRUE; }
    __attribute__((weak)) BOOL VirtualProtect(void *lpAddress, SIZE_T dwSize, DWORD flNewProtect, DWORD *lpflOldProtect) { return TRUE; }
    __attribute__((weak)) DWORD GetLastError(void) { return 0; }
    __attribute__((weak)) void SetLastError(DWORD dwErrCode) {}
    __attribute__((weak)) int InterlockedCompareExchange(LONG *Destination, LONG ExChange, LONG Comperand) {
        return __sync_val_compare_and_swap(Destination, Comperand, ExChange);
    }
    __attribute__((weak)) LONG InterlockedIncrement(LONG *Addend) { return __sync_add_and_fetch(Addend, 1); }
    __attribute__((weak)) LONG InterlockedDecrement(LONG *Addend) { return __sync_sub_and_fetch(Addend, 1); }

#ifdef __cplusplus
}
#endif

// ============================================================
// Memory allocation type
// ============================================================
typedef SIZE_T size_t_;
#define MEM_COMMIT  0x1000
#define MEM_RESERVE 0x2000
#define MEM_RELEASE 0x8000
#define MEM_DECOMMIT 0x4000

// ============================================================
// Page protection constants
// ============================================================
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOPY 0x08
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40

// ============================================================
// OutputDebugString stub
// ============================================================
#define OutputDebugStringA(s)
#define OutputDebugStringW(s)
#define OutputDebugString(s) OutputDebugStringA(s)

// ============================================================
// Assert stub
// ============================================================
#ifndef VERIFY
#define VERIFY(x) (x)
#endif

// ============================================================
// Atl/COM helper stubs
// ============================================================
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

#endif // _WINDOWS_H_STUB_
