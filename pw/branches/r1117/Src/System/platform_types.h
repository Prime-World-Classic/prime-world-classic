#ifndef __PLATFORM_TYPES_H_INCLUDED__PRIMEWORLD__
#define __PLATFORM_TYPES_H_INCLUDED__PRIMEWORLD__

#if defined(NV_LINUX_PLATFORM)

#include <cmath>
#include <stddef.h>
#include <stdint.h>

typedef unsigned long ULONG;
typedef long LONG;
typedef unsigned long long ULONGLONG;
typedef uintptr_t DWORD_PTR;
typedef intptr_t LONG_PTR;
typedef uint64_t DWORD64;
typedef int INT;
typedef int32_t INT32;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef BYTE* PBYTE;
typedef BYTE* LPBYTE;

typedef void* HANDLE;
typedef void* HINSTANCE;
typedef void* HMODULE;
typedef void* HCURSOR;
typedef void* HICON;
typedef void* HDC;
typedef void* HBITMAP;
typedef void* HWND;
typedef char* LPTSTR;
typedef const char* LPCTSTR;
typedef char* LPSTR;
typedef wchar_t* LPWSTR;
typedef const wchar_t* LPCWSTR;

typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;

typedef LRESULT (*WNDPROC)(HWND, unsigned int, WPARAM, LPARAM);

typedef unsigned short ATOM;
typedef float FLOAT;
typedef short SHORT;
typedef unsigned short USHORT;

struct POINT
{
  long x;
  long y;
};

struct SIZE
{
  long cx;
  long cy;
};

struct RECT
{
  long left;
  long top;
  long right;
  long bottom;
};

union LARGE_INTEGER
{
  struct
  {
    unsigned long LowPart;
    LONG HighPart;
  };

  long long QuadPart;
};

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef WINAPI
#define WINAPI
#endif

#ifndef EXCEPTION_BREAKPOINT
#define EXCEPTION_BREAKPOINT 0x80000003L
#endif

#ifndef WINAPIV
#define WINAPIV
#endif

#ifndef CALLBACK
#define CALLBACK
#endif

#ifndef __cdecl
#define __cdecl
#endif

#ifndef __stdcall
#define __stdcall
#endif

#ifndef interface
#define interface struct
#endif

#ifndef __int32
#define __int32 int
#endif

#ifndef __int64
#define __int64 long long
#endif

#ifndef _finite
#define _finite(value) std::isfinite(value)
#endif

#ifndef _isnan
#define _isnan(value) std::isnan(value)
#endif

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#ifndef WHEEL_DELTA
#define WHEEL_DELTA 120
#endif

#ifndef GET_WHEEL_DELTA_WPARAM
#define GET_WHEEL_DELTA_WPARAM(wParam) ((short)(((wParam) >> 16) & 0xffff))
#endif

#ifndef MAKEINTRESOURCEW
#define MAKEINTRESOURCEW(i) ((LPWSTR)((uintptr_t)((WORD)(i))))
#endif

#ifndef MAKEINTRESOURCE
#define MAKEINTRESOURCE(i) ((LPSTR)((uintptr_t)((WORD)(i))))
#endif

#endif

#endif
