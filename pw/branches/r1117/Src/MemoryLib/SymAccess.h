#ifndef SYMACCESS_H_D346DDBB_B34B_4829_9
#define SYMACCESS_H_D346DDBB_B34B_4829_9
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "../System/DefaultTypes.h"
#include "../System/systemStdAfx.h"
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct CSymString
{
	enum { N_STRING_CHARS = 128 };
	char szStr[N_STRING_CHARS];
	CSymString() { szStr[0] = 0; }
	CSymString &operator=( const char *psz ) 
	{
		szStr[N_STRING_CHARS-1] = 0; 
    #ifdef WIN32
		  strncpy_s( szStr, N_STRING_CHARS, psz, N_STRING_CHARS -1 );
		#else
		  strncpy( szStr, psz, N_STRING_CHARS-1 );
		#endif
		return *this;
	}
	bool operator ==( const char *psz ) { return strcmp( szStr, psz ) == 0; }
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct SCallStackEntry
{
  DWORD_PTR dwAddress;   // адрес кадра — указательного размера (x64: 64 бита)
  CSymString szFile, szFunc;
  int nLine;
  SCallStackEntry() : dwAddress(0), nLine(-1) {}
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef WIN32	
  #define CHANDLE HANDLE
#else
  #define CHANDLE void*
#endif  
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////  
class CSymEngine
{
	CSymEngine( const CSymEngine & ) {}
	void operator=( const CSymEngine & ) {}

  CHANDLE hProcess;
  CRITICAL_SECTION block;

  void Init();
  void Term();
public:  
  CSymEngine();
	~CSymEngine();
	
  void Lock();
  void Unlock();
  
  bool GetSymbol( QWORD dwAddress, CSymString *pszModule, CSymString *pszFile, int *pnLine, CSymString *pszFunc );

  int QuickCollectCallStack( DWORD *addresses, int maxEntries, int skipEntries );

  int CollectCallStack( SCallStackEntry *callStack, int maxEntries, int skipEntries );
  int CollectCallStack( SCallStackEntry *callStack, int maxEntries, int skipEntries, const CONTEXT *context );
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void AssignSymEngine( CSymEngine* symEngine );
// Движок назначен только на время жизни статика RegisterDumpMemoryLeaks:
// new/delete случаются и до его конструктора, и после деструктора, когда
// globalSymEngine == 0 -> Lock() по null+offset = AV. Проверять перед Lock().
CSymEngine* GetSymEngine();
CSymEngine& LockSymEngine();
void ReleaseSymEngine();
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#endif //#define SYMACCESS_H_D346DDBB_B34B_4829_9