#pragma once

//UGLY HACK CRAZY
//Defining _NEW_ macro to bypass standard "new" header
#ifndef _NEW_
#define _NEW_

#define _INC_NEW
#include <malloc.h>

// comment this define to use CRT new (directly mapped to HeapAlloc), very usefull when diagnose memory vie detour or umdh.exe
#define CHECK_MEMORY_LEAKS

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ������������� ���������� � ������������� ������ ������������ ���������� � ���������� �����
//
//  �������� �����:
//    � placement new: �������������������� ����������
//    � ������� new / delete: �������������������� ����������, ����� �� ������� (����� � ������), 
//    ������ � ������ ����� ������������, ��������� ����� delete ��� ����� delete �� �������������
//    ���������
//
//  �� ��������� ��������� (�.�. ������). ��� ��������� ����� ����������  ALLOC_DATA_DEBUG_INIT � 
//  newdelete.h (�������� "&& 0" �� "&& 1" )
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if !defined(_SHIPPING) && 0
  #define ALLOC_DATA_DEBUG_INIT
#endif


//We cannot use defines from 'System'
#ifdef WIN32
#define NEWDEL_CCDECL    __cdecl
#define NEWDEL_RETARG    _Ret_bytecap_(_Size)
#else
#define NEWDEL_CCDECL    __attribute__ ((cdecl))
//In last GCC version alloc_size is "known"...
//#define NEWDEL_RETARG    __attribute__ ((alloc_size(1)))
#define NEWDEL_RETARG
#endif  

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef CHECK_MEMORY_LEAKS
void* NEWDEL_CCDECL operator new( size_t size );
void NEWDEL_CCDECL operator delete( void *p );
void* NEWDEL_CCDECL operator new[](size_t size);
void NEWDEL_CCDECL operator delete[]( void * p );
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// we need this declarations due wx use it (and UniServer is wx application due need of parsing XML)
NEWDEL_RETARG void* NEWDEL_CCDECL operator new(size_t size, const char *, int );
NEWDEL_RETARG void* NEWDEL_CCDECL operator new(size_t size, int , const char *, int );
NEWDEL_RETARG void* NEWDEL_CCDECL operator new[](size_t size, const char *, int );
NEWDEL_RETARG void* NEWDEL_CCDECL operator new[](size_t size, int , const char *, int );
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#endif // #ifdef CHECK_MEMORY_LEAKS
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef WIN32
// MSVC 14.x (VS2017+, _MSC_VER >= 1910): placement new/delete уже определены со
// телом в <vcruntime_new.h> (у него #pragma once, наш хак с _NEW_/_INC_NEW его не
// блокирует) — собственное определение даёт «error C2084: function
// 'operator new(void *,void *) noexcept' already has a body».
#if !defined(_MSC_VER) || _MSC_VER < 1910
#ifndef ALLOC_DATA_DEBUG_INIT
  inline void* NEWDEL_CCDECL operator new(size_t, void *p) { return (p); }
#else
  //���� ������ static � ����������, �� ������ ������� ������������ � ATL � ��� ����� 
  //� �� ������, ��� �������� �� ��������� ��������������� placement new
  inline static void* NEWDEL_CCDECL operator new(size_t size, void *p) 
  { 
    memset(p, 0xCD, size);
    return p;
  }
#endif

inline void NEWDEL_CCDECL operator delete(void *, void *) { return; }
#endif // _MSC_VER < 1910 (placement new/delete из <vcruntime_new.h>)
#endif // WIN32
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// define ::std::nothrow tag due ACE use it
#ifndef __NOTHROW_T_DEFINED
#define __NOTHROW_T_DEFINED
#ifdef WIN32
namespace std
{
struct nothrow_t
{
};
extern const nothrow_t nothrow;	// constant for placement new tag
}
#endif // WIN32
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void* NEWDEL_CCDECL operator new(size_t size, const std::nothrow_t&);
void* NEWDEL_CCDECL operator new[](size_t size, const std::nothrow_t&);
void NEWDEL_CCDECL operator delete(void * p, const std::nothrow_t&);
void NEWDEL_CCDECL operator delete[](void * p, const std::nothrow_t&);
#endif //#ifndef __NOTHROW_T_DEFINED
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void* Aligned_MAlloc(size_t size, size_t alignment);
void Aligned_Free(void* ptr);
void ForcedDeleteHack( void *p );
void* NEWDEL_CCDECL Realloc(void* p, size_t size);
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
long GetMallocsTotal();
long GetMallocsUnfree();
long GetMallocsSize();
size_t GetAllocatedFootprint();    // info from allocator library

// ����������� ������ ������� ��������� (top-N ������ �� �������). �����, �����
// �������� ���������� ������ � ������ ����: �� ������ �������� g_unfreeMaloc == 0
// (������ �� �����������), � ������� ���� ������ ������ �� ����������.
void DumpMemoryLeaksNow();
void SetMemoryLeaksDumpLevel( bool dumpOnlyAmount );
void SetMallocThreadMask( unsigned long threadId );

#define DECLARE_NEWDELETE_ALIGN16(className)                                              \
static void* operator new(size_t size) { return Aligned_MAlloc(sizeof(className), 16); }  \
static void operator delete(void* ptr)	{ Aligned_Free(ptr); }

#endif //_NEW_

// Set of types and functions for performance logging, see PERF_LOG_NEWDELETE in newdelete.cpp for details
typedef void (* LogAllocCallback)( int mallocIndex, int size, void *pData, unsigned long *stack, int stackSize );
typedef void (* LogFreeCallback)( int mallocIndex );
void SetPerformanceAllocsEnabled( bool enabled, LogAllocCallback allocCallback, LogFreeCallback freeCallback );


namespace ni_detail
{

typedef void (* TCommonMemoryCallback )( int allocIndex, size_t size );

bool AddMemoryCallback( TCommonMemoryCallback callback, bool forAllocs );
bool RemoveMemoryCallback( TCommonMemoryCallback callback, bool forAllocs );

} //namespace ni_detail

