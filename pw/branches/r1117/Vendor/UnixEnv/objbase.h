// Stub objbase.h for Linux (no COM support)
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifndef _OBJBASE_H_STUB_
#define _OBJBASE_H_STUB_

// Basic Windows types needed by COM
#ifndef __cplusplus
typedef unsigned char BYTE;
#endif

typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t BYTE_;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int BOOL_;

#ifndef BOOL
#define BOOL int
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef WINAPI
#define WINAPI
#endif

#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE
#endif

// GUID structure
typedef struct _GUID {
    unsigned long  Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char  Data4[8];
} GUID;

typedef GUID CLSID;
typedef GUID IID;
typedef const GUID *REFCLSID;
typedef const GUID *REFIID;

// DEFINE_GUID macro
#ifdef INITGUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    EXTERN_C const GUID DECLSPEC_SELECTANY name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }
#else
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    EXTERN_C const GUID name
#endif

// Storage class specifiers (empty on Linux)
#ifndef DECLSPEC_SELECTANY
#ifdef __GNUC__
#define DECLSPEC_SELECTANY __attribute__((weak))
#else
#define DECLSPEC_SELECTANY
#endif
#endif

#ifndef DECLSPEC_UUID
#define DECLSPEC_UUID(x)
#endif

#ifndef EXTERN_C
#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C extern
#endif
#endif

// IUnknown interface (stub)
#ifdef __cplusplus
struct IUnknown {
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) = 0;
    virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
    virtual ULONG STDMETHODCALLTYPE Release() = 0;
};
#endif

// HRESULT type
#ifndef _HRESULT_DEFINED
#define _HRESULT_DEFINED
typedef unsigned long HRESULT;
#endif

// COM inline functions
#define S_OK            0L
#define S_FALSE         0x0001L
#define E_FAIL          0x80004005L
#define E_NOINTERFACE   0x80004002L
#define E_NOTIMPL       0x80004001L
#define E_OUTOFMEMORY   0x8007000EL
#define E_POINTER       0x80004003L
#define E_UNEXPECTED    0x8000FFFFL
#define CO_E_INIT_NOT_INITIALIZED 0x800401F0L

// CoInitialize stub
#ifdef __cplusplus
extern "C" {
#endif
    HRESULT __attribute__((weak)) CoInitialize(void *pvReserved) { return S_OK; }
    HRESULT __attribute__((weak)) CoInitializeEx(void *pvReserved, DWORD dwCoInit) { return S_OK; }
    void __attribute__((weak)) CoUninitialize(void) {}
    void* __attribute__((weak)) CoTaskMemAlloc(size_t cb) { return malloc(cb); }
    void __attribute__((weak)) CoTaskMemFree(void *pv) { free(pv); }
    void* __attribute__((weak)) CoTaskMemRealloc(void *pv, size_t cb) { return realloc(pv, cb); }
    HRESULT __attribute__((weak)) CLSIDFromProgWStr(const wchar_t *lpszProgID, CLSID *pclsid) { return E_FAIL; }
    HRESULT __attribute__((weak)) CLSIDFromProgID(const char *lpszProgID, CLSID *pclsid) { return E_FAIL; }
#ifdef __cplusplus
}
#endif

// Interlocked functions (stub - use C11 atomics on Linux)
#ifdef __cplusplus
extern "C" {
#endif
    LONG __attribute__((weak)) InterlockedIncrement(LONG *Addend) { return __sync_add_and_fetch(Addend, 1); }
    LONG __attribute__((weak)) InterlockedDecrement(LONG *Addend) { return __sync_sub_and_fetch(Addend, 1); }
    LONG __attribute__((weak)) InterlockedCompareExchange(LONG *Destination, LONG ExChange, LONG Comperand) {
        return __sync_val_compare_and_swap(Destination, Comperand, ExChange);
    }
#ifdef __cplusplus
}
#endif

// Inline ISuccess/HResult check macros
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)

#endif // _OBJBASE_H_STUB_
