#ifndef BSUTIL_H_
#define BSUTIL_H_

#include "StackWalk.h"

struct EXCEPTION_POINTERS;
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
namespace NBSU
{
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
enum EBSUReport
{
    BSU_DEFAULT,
    BSU_ABORT,
    BSU_DEBUG,
    BSU_IGNORE,
    BSU_IGNORE_ALL,
    BSU_CONTINUE,
    BSU_MAKE_FULL_MINIDUMP,
    BSU_MAKE_MINI_MINIDUMP,
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct SIgnoresEntry
{
	string szFunctionName;
	string szFileName;
	int nLineNumber;
	//
	SIgnoresEntry( const char *_szFunctionName, const char *_szFileName, int _nLineNumber ) :
		szFunctionName( _szFunctionName ), szFileName( _szFileName ), nLineNumber( _nLineNumber ) {}
	//
	bool operator==( const SIgnoresEntry &ig ) const { return ( szFileName == ig.szFileName ) && ( nLineNumber == ig.nLineNumber ); }
};
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef list<SIgnoresEntry> TIgnoresList;

typedef void  (*ExceptionCallback)(void);

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if defined( NV_LINUX_PLATFORM )

inline bool IsIgnore( const char *, int ) { return false; }
inline void AddIgnore( const char *, const char *, int ) {}
inline void RemoveIgnore( const char *, int ) {}
inline void SetIgnoreAll( bool ) {}
inline const TIgnoresList &GetIgnoresList()
{
  static const TIgnoresList ignores;
  return ignores;
}
inline void WriteAssertLogFile( const struct tm&, const char *, const vector<SCallStackEntry>&, bool ) {}
inline void WriteExceptionLogFile( const struct tm&, const EXCEPTION_POINTERS*, const vector<SCallStackEntry>&, string * = 0 ) {}
inline HINSTANCE GetBSUInstance() { return 0; }
inline void SetBSUWindow( HWND ) {}
inline HWND GetBSUWindow() { return 0; }
inline void InitUnhandledExceptionHandler() {}
inline void SetExceptionCallback( ExceptionCallback ) {}

#else

bool IsIgnore( const char *pszFileName, int nLineNumber );
void AddIgnore( const char *pszFunctionName, const char *pszFileName, int nLineNumber );
void RemoveIgnore( const char *pszFileName, int nLineNumber );
void SetIgnoreAll( bool ignoreAll );
const TIgnoresList &GetIgnoresList();
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WriteAssertLogFile(    const struct tm& tim, const char *message, const vector<SCallStackEntry>& entries, bool dataAssert );
void WriteExceptionLogFile( const struct tm& tim, const EXCEPTION_POINTERS* pExceptionInfo, const vector<SCallStackEntry>& entries, string * pFileName = 0 );
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
HINSTANCE GetBSUInstance();
void SetBSUWindow( HWND hWindow );
HWND GetBSUWindow();
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void InitUnhandledExceptionHandler();
void SetExceptionCallback(ExceptionCallback callb);

#endif
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif
