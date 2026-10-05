// Stub objbase.h for Linux (no COM support)
// This file includes windows.h first, then adds COM-specific types
#pragma once

#include "windows.h"

#ifndef _OBJBASE_H_STUB_
#define _OBJBASE_H_STUB_

// ============================================================
// GUID structure
// ============================================================
#ifndef _GUID_DEFINED
typedef struct _GUID {
    unsigned long  Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char  Data4[8];
} GUID;
#define _GUID_DEFINED
#endif

typedef GUID CLSID;
typedef GUID IID;
typedef const GUID *REFCLSID;
typedef const GUID *REFIID;

// ============================================================
// DEFINE_GUID macro
// ============================================================
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

#ifdef INITGUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    EXTERN_C const GUID DECLSPEC_SELECTANY name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }
#else
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    EXTERN_C const GUID name
#endif

// ============================================================
// IUnknown interface
// ============================================================
#ifdef __cplusplus
struct IUnknown {
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) = 0;
    virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
    virtual ULONG STDMETHODCALLTYPE Release() = 0;
    virtual ~IUnknown() {}
};
#endif

// ============================================================
// CoInitialize / COM functions
// ============================================================
#ifndef CO_E_INIT_NOT_INITIALIZED
#define CO_E_INIT_NOT_INITIALIZED 0x800401F0L
#endif

#ifdef __cplusplus
extern "C" {
#endif
    __attribute__((weak)) HRESULT CoInitialize(void *pvReserved) { return S_OK; }
    __attribute__((weak)) HRESULT CoInitializeEx(void *pvReserved, DWORD dwCoInit) { return S_OK; }
    __attribute__((weak)) void CoUninitialize(void) {}
    __attribute__((weak)) void* CoTaskMemAlloc(size_t cb) { return malloc(cb); }
    __attribute__((weak)) void CoTaskMemFree(void *pv) { free(pv); }
    __attribute__((weak)) void* CoTaskMemRealloc(void *pv, size_t cb) { return realloc(pv, cb); }
    __attribute__((weak)) HRESULT CLSIDFromProgWStr(const wchar_t *lpszProgID, CLSID *pclsid) { return E_FAIL; }
    __attribute__((weak)) HRESULT CLSIDFromProgID(const char *lpszProgID, CLSID *pclsid) { return E_FAIL; }
#ifdef __cplusplus
}
#endif

// ============================================================
// Interlocked functions
// ============================================================
#ifndef _INTERLOCKED_DEFINED
#ifdef __cplusplus
extern "C" {
#endif
    __attribute__((weak)) int InterlockedCompareExchange(LONG *Destination, LONG ExChange, LONG Comperand) {
        return __sync_val_compare_and_swap(Destination, Comperand, ExChange);
    }
    __attribute__((weak)) LONG InterlockedIncrement(LONG *Addend) { return __sync_add_and_fetch(Addend, 1); }
    __attribute__((weak)) LONG InterlockedDecrement(LONG *Addend) { return __sync_sub_and_fetch(Addend, 1); }
#ifdef __cplusplus
}
#endif
#define _INTERLOCKED_DEFINED
#endif

// ============================================================
// SUCCEEDED/FAILED macros
// ============================================================
#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#endif
#ifndef FAILED
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#endif

// ============================================================
// __uuidof stub (for MSVC extension compatibility)
// ============================================================
#ifdef __cplusplus
// __uuidof is MSVC-specific; on GCC/Clang we can use __builtin_extracting_return_type
// but for now just provide a no-op that works with DECLSPEC_UUID
#define __uuidof(x) (*(const GUID*)(0))
#endif

#endif // _OBJBASE_H_STUB_
