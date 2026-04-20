#ifndef __WIN32_LINUX_H_UNIQUE__
#define __WIN32_LINUX_H_UNIQUE__

#define THREAD_PRIORITY_LOWEST          -2
#define THREAD_PRIORITY_BELOW_NORMAL    -1
#define THREAD_PRIORITY_NORMAL          0
#define THREAD_PRIORITY_HIGHEST         2
#define THREAD_PRIORITY_ABOVE_NORMAL    1

#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <malloc.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

#include "System/types.h"

typedef char* PSTR;
typedef unsigned char* PUCHAR;
typedef int BOOLEAN;
typedef struct _LIST_ENTRY {
   struct _LIST_ENTRY *Flink;
   struct _LIST_ENTRY *Blink;
} LIST_ENTRY;
typedef uint64_t DWORD64;
typedef uint64_t ULONG64;
typedef uint32_t ULONG32;
typedef uint32_t RVA;
typedef uint64_t RVA64;

#define _malloca malloc
#define _freea free
#define EXCEPTION_BREAKPOINT 0x80000003L
#define _vsnprintf_s vsnprintf_s


// Limits
#define _I64_MAX LLONG_MAX
#define _I64_MIN LLONG_MIN
#define _I32_MAX INT_MAX
#define _I32_MIN INT_MIN

// Basic types
typedef uint8_t       BYTE;
typedef uint16_t      WORD;
typedef uint32_t      DWORD;
typedef int32_t       BOOL;
typedef uint32_t      UINT;
typedef int32_t       INT;
typedef int32_t       LONG;
#ifndef MAKELONG
#define MAKELONG(a, b) ((LONG)(((WORD)(((DWORD_PTR)(a)) & 0xffff)) | ((DWORD)((WORD)(((DWORD_PTR)(b)) & 0xffff))) << 16))
#endif
#ifndef MAKEWORD
#define MAKEWORD(a, b) ((WORD)(((BYTE)(((DWORD_PTR)(a)) & 0xff)) | ((WORD)((BYTE)(((DWORD_PTR)(b)) & 0xff))) << 8))
#endif
typedef uint32_t      ULONG;
typedef uintptr_t     ULONG_PTR;
typedef uintptr_t     DWORD_PTR;
typedef int64_t       LONGLONG;
typedef uint64_t      ULONGLONG;
typedef uint64_t      UINT64;
typedef uint32_t      UINT32;
typedef uint8_t       UCHAR;
typedef int16_t       SHORT;
typedef uint16_t      USHORT;
typedef float         FLOAT;
typedef double        DOUBLE;
typedef size_t        SIZE_T;
typedef uintptr_t     UINT_PTR;

#define VOID void
typedef void*         PVOID;
typedef void*         LPVOID;
typedef const void*   LPCVOID;

#define __int64 long long
#define __int32 int
#define __int16 short
#define __int8 char

typedef int           errno_t;

typedef char          CHAR;
typedef char*         LPSTR;
typedef const char*   LPCSTR;
typedef wchar_t       WCHAR;
typedef wchar_t*      LPWSTR;
typedef const wchar_t* LPCWSTR;
typedef unsigned char* LPBYTE;
typedef uint8_t*      PBYTE;
typedef uint32_t*     PDWORD;
typedef uint32_t*     LPDWORD;
typedef BOOL*         LPBOOL;

#ifdef UNICODE
typedef WCHAR         TCHAR;
typedef LPCWSTR       LPCTSTR;
typedef LPWSTR        LPTSTR;
#define TEXT(x)       L##x
#else
typedef CHAR          TCHAR;
typedef LPCSTR        LPCTSTR;
typedef LPSTR         LPTSTR;
#define TEXT(x)       x
#endif

typedef void*         HANDLE;
typedef void*         HINSTANCE;
typedef void*         HWND;
typedef void*         HMODULE;
typedef void*         HICON;
typedef void*         HCURSOR;
typedef void*         HBRUSH;
typedef void*         HMENU;
typedef void*         HGDIOBJ;
typedef void*         HDC;
typedef void*         HKEY;
typedef void**        PHKEY;

typedef int           HRESULT;

#define S_OK           ((HRESULT)0L)
#define S_FALSE        ((HRESULT)1L)
#define E_FAIL         ((HRESULT)0x80004005L)
#define E_OUTOFMEMORY  ((HRESULT)0x8007000EL)
#define E_NOINTERFACE  ((HRESULT)0x80004002L)
#define E_POINTER      ((HRESULT)0x80004003L)
#define E_PENDING ((HRESULT)0x8000000AL)
#define E_INVALIDARG ((HRESULT)0x80070057L)

#define TRUE  1
#define FALSE 0

#define MB_OK                       0x00000000L
#define MB_ICONINFORMATION          0x00000040L
#define MB_ICONWARNING              0x00000030L
#define MB_SETFOREGROUND            0x00010000L
#define MB_TOPMOST                  0x00040000L

#define MB_ICONSTOP                 0x00000010L
#define MB_TASKMODAL                0x00002000L
#define MB_SERVICE_NOTIFICATION     0x00200000L


#define ERROR_SUCCESS               0L

#ifndef CONST
#define CONST const
#endif

#define WINAPI
#define CALLBACK
#define STDMETHODCALLTYPE
#define WINAPIV
#define __stdcall
#define __cdecl
#define __forceinline inline
#define STDAPI HRESULT WINAPI

#define __declspec(x)


#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))

typedef struct _DEVMODE {
  char  dmDeviceName[32];
  WORD  dmSpecVersion;
  WORD  dmDriverVersion;
  WORD  dmSize;
  WORD  dmDriverExtra;
  DWORD dmFields;
  short dmOrientation;
  short dmPaperSize;
  short dmPaperLength;
  short dmPaperWidth;
  short dmScale;
  short dmCopies;
  short dmDefaultSource;
  short dmPrintQuality;
  short dmColor;
  short dmDuplex;
  short dmYResolution;
  short dmTTOption;
  short dmCollate;
  char  dmFormName[32];
  WORD  dmLogPixels;
  DWORD dmBitsPerPel;
  DWORD dmPelsWidth;
  DWORD dmPelsHeight;
  DWORD dmDisplayFlags;
  DWORD dmDisplayFrequency;
  DWORD dmICMMethod;
  DWORD dmICMIntent;
  DWORD dmMediaType;
  DWORD dmDitherType;
  DWORD dmReserved1;
  DWORD dmReserved2;
  DWORD dmPanningWidth;
  DWORD dmPanningHeight;
} DEVMODE;

#define ENUM_REGISTRY_SETTINGS ((DWORD)-1)
inline BOOL EnumDisplaySettings(const char*, DWORD, DEVMODE*) { return FALSE; }

#define MAX_PATH 260
#define THREAD_LS __thread

#define ZeroMemory(Destination,Length) memset((Destination),0,(Length))
#define CopyMemory(Destination,Source,Length) memcpy((Destination),(Source),(Length))

#define _snprintf_s(buf, size, ...) snprintf(buf, size, __VA_ARGS__)
#define _vsnprintf_s(buf, size, fmt, va) vsnprintf(buf, size, fmt, va)
#define _vsnwprintf_s(buf, size, fmt, va) vswprintf(buf, size, fmt, va)
#define _ultoa_s(val, buf, radix) sprintf(buf, "%lu", val)
#define _wtoi(str) wcstol(str, NULL, 10)
#define _wcsnicmp wcsncasecmp
#define _strnicmp strncasecmp
#define wcscpy_s(dest, destsz, src) (wcscpy(dest, src), 0)
#define WideCharToMultiByte(cp, flags, src, cchSrc, dest, cbDest, defChar, usedDef) wcstombs(dest, src, cbDest)
#define MultiByteToWideChar(cp, flags, src, cbSrc, dest, cchDest) mbstowcs(dest, src, cchDest)
#define GetACP() 0
#define gmtime_s(tm, time) gmtime_r(time, tm)

#define MAKE_HRESULT(sev,fac,code) \
    ((HRESULT) (((unsigned long)(sev)<<31) | ((unsigned long)(fac)<<16) | ((unsigned long)(code))))

typedef struct GUID {
    DWORD Data1;
    WORD  Data2;
    WORD  Data3;
    BYTE  Data4[8];
} GUID, *LPGUID;

typedef const GUID& REFIID;
typedef const GUID& REFGUID;

#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    extern "C" const GUID name = { (DWORD)l, (WORD)w1, (WORD)w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }

inline HRESULT UuidCreate(GUID* p) { memset(p, 0, sizeof(GUID)); return S_OK; }
inline HRESULT CoCreateGuid(GUID* p) { return UuidCreate(p); }
inline BOOL IsEqualGUID(REFGUID rguid1, REFGUID rguid2) { return memcmp(&rguid1, &rguid2, sizeof(GUID)) == 0; }

typedef struct POINT {
    LONG x;
    LONG y;
} POINT;

typedef struct RECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *LPRECT;

typedef struct SIZE {
    LONG cx;
    LONG cy;
} SIZE;

typedef struct _LARGE_INTEGER {
    union {
        struct {
            DWORD LowPart;
            LONG HighPart;
        };
        struct {
            DWORD LowPart;
            LONG HighPart;
        } u;
        LONGLONG QuadPart;
    };
} LARGE_INTEGER;

typedef struct _LUID {
    DWORD LowPart;
    LONG  HighPart;
} LUID, *PLUID;

typedef struct tagPALETTEENTRY {
    BYTE peRed;
    BYTE peGreen;
    BYTE peBlue;
    BYTE peFlags;
} PALETTEENTRY;

typedef struct _RGNDATAHEADER {
    DWORD dwSize;
    DWORD iType;
    DWORD nCount;
    DWORD nRgnSize;
    RECT  rcBound;
} RGNDATAHEADER;

typedef struct _RGNDATA {
    RGNDATAHEADER rdh;
    char          Buffer[1];
} RGNDATA;

#define LF_FACESIZE 32
typedef struct tagTEXTMETRICA {
    LONG tmHeight;
} TEXTMETRICA;
typedef struct tagTEXTMETRICW {
    LONG tmHeight;
} TEXTMETRICW;

typedef struct _CRITICAL_SECTION {
    void* DebugInfo;
    LONG LockCount;
    LONG RecursionCount;
    HANDLE OwningThread;
    HANDLE LockSemaphore;
    ULONG_PTR SpinCount;
} CRITICAL_SECTION;

typedef struct _CONTEXT {
    DWORD ContextFlags;
} CONTEXT;

typedef struct _EXCEPTION_POINTERS {
    void* ExceptionRecord;
    void* ContextRecord;
} EXCEPTION_POINTERS;

typedef struct _OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union {
        struct {
            DWORD Offset;
            DWORD OffsetHigh;
        };
        PVOID Pointer;
    };
    HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;


#define DECLARE_HANDLE(name) typedef void* name
#define DECLSPEC_UUID(x)
#ifndef interface
#define interface struct
#endif
#define STDMETHOD(method) virtual HRESULT STDMETHODCALLTYPE method
#define STDMETHOD_(type,method) virtual type STDMETHODCALLTYPE method
#define STDMETHODV(method) virtual HRESULT STDMETHODCALLTYPE method
#define STDMETHODV_(type,method) virtual type STDMETHODCALLTYPE method
#define PURE = 0
#define THIS_
#define THIS

#define DECLARE_INTERFACE(iface)    interface iface
#define DECLARE_INTERFACE_(iface, baseiface)    interface iface : public baseiface

#ifdef __cplusplus
interface IUnknown {
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) = 0;
    virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
    virtual ULONG STDMETHODCALLTYPE Release() = 0;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif


inline BOOL QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    lpPerformanceCount->QuadPart = (LONGLONG)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return TRUE;
}
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency) {
    lpFrequency->QuadPart = 1000000000LL;
    return TRUE;
}

inline DWORD GetTickCount() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

inline HANDLE CreateFileA(const char*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE) { return (HANDLE)-1; }
inline HANDLE CreateFileW(const wchar_t*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE) { return (HANDLE)-1; }
#define CreateFile CreateFileA

inline BOOL CloseHandle(HANDLE) { return TRUE; }
inline DWORD GetLastError() { return 0; }
inline void SleepEx(DWORD, BOOL) {}
inline DWORD GetCurrentProcessId() { return (DWORD)getpid(); }
inline HANDLE GetCurrentProcess() { return (HANDLE)(long)getpid(); }
inline BOOL TerminateProcess(HANDLE, UINT) { return TRUE; }
inline DWORD GetCurrentThreadId() { return (DWORD)pthread_self(); }

inline int MessageBoxA(HWND, LPCSTR, LPCSTR, UINT) { return 0; }
inline int MessageBoxW(HWND, LPCWSTR, LPCWSTR, UINT) { return 0; }
#ifdef UNICODE
#define MessageBox MessageBoxW
#else
#define MessageBox MessageBoxA
#endif


inline DWORD GetFileSize(HANDLE, DWORD*) { return 0; }
inline DWORD SetFilePointer(HANDLE, LONG, LONG*, DWORD) { return 0; }
inline BOOL SetEndOfFile(HANDLE) { return TRUE; }

typedef void (WINAPI *LPOVERLAPPED_COMPLETION_ROUTINE)(DWORD, DWORD, LPOVERLAPPED);
inline BOOL WriteFileEx(HANDLE, const void*, DWORD, LPOVERLAPPED, LPOVERLAPPED_COMPLETION_ROUTINE) { return TRUE; }
inline BOOL ReadFileEx(HANDLE, void*, DWORD, LPOVERLAPPED, LPOVERLAPPED_COMPLETION_ROUTINE) { return TRUE; }
inline BOOL WriteFile(HANDLE, const void*, DWORD, DWORD*, LPOVERLAPPED) { return TRUE; }

#define GENERIC_READ 0x80000000L
#define GENERIC_WRITE 0x40000000L
#define FILE_SHARE_READ 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_FLAG_NO_BUFFERING 0x20000000
#define FILE_FLAG_OVERLAPPED 0x40000000
#define INVALID_HANDLE_VALUE ((HANDLE)(long)-1)
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2

#ifdef __cplusplus
} // extern "C"
#endif

#ifndef NI_OUTPUT_DEBUG_STRING_DEFINED
#define NI_OUTPUT_DEBUG_STRING_DEFINED
#ifdef __cplusplus
static inline void OutputDebugStringA(const char*) {}
static inline void OutputDebugStringW(const wchar_t*) {}
#define OutputDebugString NiOutputDebugStringA
#else
#define OutputDebugStringA(x) ((void)0)
#define OutputDebugStringW(x) ((void)0)
#define OutputDebugString(x) ((void)0)
#endif
#define OutputDebugString OutputDebugStringA
#endif

#define _snwprintf_s swprintf
#define swprintf_s swprintf
#define sscanf_s sscanf
#define _stricmp strcasecmp
#define _wcsicmp wcscasecmp
#define _countof(a) (sizeof(a)/sizeof(*(a)))

#define memcpy_s(dest, dest_size, src, src_size) memcpy(dest, src, src_size)
#define localtime_s(tm_ptr, time_t_ptr) localtime_r(time_t_ptr, tm_ptr)

#ifdef __cplusplus
template <size_t size>
inline int sprintf_s(char (&buffer)[size], const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(buffer, size, format, args);
    va_end(args);
    return ret;
}

inline int fopen_s(FILE** pFile, const char* filename, const char* mode) {
    *pFile = fopen(filename, mode);
    return *pFile ? 0 : errno;
}

inline int sprintf_s(char *buffer, size_t sizeOfBuffer, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(buffer, sizeOfBuffer, format, args);
    va_end(args);
    return ret;
}

template <size_t size>
inline int strcpy_s(char (&buffer)[size], const char *src) {
    strncpy(buffer, src, size);
    return 0;
}
inline int strcpy_s(char *dest, size_t size, const char *src) {
    strncpy(dest, src, size);
    return 0;
}

template <size_t size>
inline int vsnprintf_s(char (&buffer)[size], size_t count, const char *format, va_list args) {
    return vsnprintf(buffer, count == (size_t)-1 ? size : (count > size ? size : count), format, args);
}
inline int vsnprintf_s(char *buffer, size_t sizeOfBuffer, size_t count, const char *format, va_list args) {
    return vsnprintf(buffer, count == (size_t)-1 ? sizeOfBuffer : (count > sizeOfBuffer ? sizeOfBuffer : count), format, args);
}
#endif

#ifdef __cplusplus
// Overloads for Interlocked functions to handle both LONG (32-bit) and long (64-bit)
inline LONG InterlockedIncrement(LONG volatile* lpAddend) { return __sync_add_and_fetch(lpAddend, 1); }
inline long InterlockedIncrement(long volatile* lpAddend) { return __sync_add_and_fetch(lpAddend, 1); }
inline LONG InterlockedDecrement(LONG volatile* lpAddend) { return __sync_sub_and_fetch(lpAddend, 1); }
inline long InterlockedDecrement(long volatile* lpAddend) { return __sync_sub_and_fetch(lpAddend, 1); }
inline LONG InterlockedExchange(LONG volatile* Target, LONG Value) { return __sync_lock_test_and_set(Target, Value); }
inline long InterlockedExchange(long volatile* Target, long Value) { return __sync_lock_test_and_set(Target, Value); }
#else
inline LONG InterlockedIncrement(LONG volatile* lpAddend) { return __sync_add_and_fetch(lpAddend, 1); }
inline LONG InterlockedDecrement(LONG volatile* lpAddend) { return __sync_sub_and_fetch(lpAddend, 1); }
inline LONG InterlockedExchange(LONG volatile* Target, LONG Value) { return __sync_lock_test_and_set(Target, Value); }
#endif

#ifdef __cplusplus
#include <d3d9types.h>
#endif

#define D3DUSAGE_DMAP 0x00004000L

typedef void* IStream;
typedef float* LPGLYPHMETRICSFLOAT;

typedef void (* LPD3DXFILL2D)(struct D3DXVECTOR4*, const struct D3DXVECTOR2*, const struct D3DXVECTOR2*, LPVOID);
typedef void (* LPD3DXFILL3D)(struct D3DXVECTOR4*, const struct D3DXVECTOR3*, const struct D3DXVECTOR3*, LPVOID);

#ifndef _NV_LONG_DEFINED_UNIQUE_
#define _NV_LONG_DEFINED_UNIQUE_
#endif
#ifndef BYTE_DEFINED
#define BYTE_DEFINED
#endif
#ifndef WORD_DEFINED
#define WORD_DEFINED
#endif
#ifndef DWORD_DEFINED
#define DWORD_DEFINED
#endif
#ifndef BOOL_DEFINED
#define BOOL_DEFINED
#endif
#ifndef UINT_DEFINED
#define UINT_DEFINED
#endif
#ifndef _NV_UINT64_DEFINED_
#define _NV_UINT64_DEFINED_
#endif
#ifndef GUID_DEFINED
#define GUID_DEFINED
#endif
#ifndef _NV_HRESULT_DEFINED_
#define _NV_HRESULT_DEFINED_
#endif

#define _EM_INVALID 0
#define _EM_ZERODIVIDE 0
#define _EM_OVERFLOW 0
#define _EM_UNDERFLOW 0
#define _EM_INEXACT 0
#define _EM_DENORMAL 0
#define _PC_24 0
#define _PC_64 0
#define _RC_NEAR 0
#define NI_SYNC_FPU_START
#define NI_SYNC_FPU_END

#define _copysign copysign
inline long _wtol(const wchar_t *str) { return wcstol(str, NULL, 10); }

#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr)    (((HRESULT)(hr)) < 0)

#endif // __WIN32_LINUX_H_UNIQUE__
