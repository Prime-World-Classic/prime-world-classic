#include "stdafx.h"
#include "StackWalk.h"
#if defined( NV_WIN_PLATFORM )
#include "../MemoryLib/SymAccess.h"
#include "StackWalker.h"
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef vector<SCallStackEntry> CallStackVector;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ����� ��������� �������� Stack Trace ��� ������ ����������
// http://stackwalker.codeplex.com/
// 
// ����������� � ����� � ���, ��� ������������ �� ��� ��� �������� �� ��������� �������� 
// � ��������� ��� Wine.
//
// ��� ��� ��������� �����, ��� ������� �� ���� ������� �������, ����� ��������������� 
// ���������� ������ � �������� ������������ ���������� ������ dll. ��� ���������,
// ������������ ������ callstack, ��� ����������� �� ���� �� ������ ������ ���� ��������� dll.
// 
// ��� ����, ����� ��� ������� ��-��� wine ����� ���� �������� ����������� callstack
// ����� ��������� dbghelp.dll �� Windows � ����� � ���������� �, �������������, � winecfg 
// ���������� ��������� ��� ���������� dbghelp (dbghelp ����������� ��������� ������� � ������ 
// ��������� dll �� ����������)
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class AdaptedStackWalker: public StackWalker, NonCopyable
{
public:
  explicit AdaptedStackWalker( CallStackVector &vec ): vec(vec)
  {
    vec.clear();
    vec.reserve(128);
  }  

  virtual void OnLoadModule( LPCSTR img, LPCSTR mod, DWORD64 baseAddr, DWORD size, DWORD result, LPCSTR symType, LPCSTR pdbName, ULONGLONG fileVersion ) 
  {
    StackWalker::OnLoadModule( img, mod, baseAddr, size, result, symType, pdbName, fileVersion );   
    addrToModName.insert( make_pair(baseAddr, mod) ); 
  }
  
  virtual void OnDbgHelpErr( LPCSTR szFuncName, DWORD gle, DWORD64 addr ) 
  {
    StackWalker::OnDbgHelpErr( szFuncName, gle, addr );
    
    //���� �����������������, �� ������ �������� �������������
    //AppendAddressOnly( addr, "" );
  }
  
  virtual void OnCallstackEntry( CallstackEntryType eType, CallstackEntry &entry ) 
  {   
    if ( (eType != lastEntry) && (entry.offset != 0) )
    {
      if ( entry.lineFileName[0] == '\0' )
        AppendAddressOnly( entry.offset, entry.name );  
      else
      {
        SCallStackEntry &item = vec.push_back();
    
        item.szFile = entry.lineFileName;
        item.szFunc = entry.name;
        item.nLine  = entry.lineNumber;
        item.dwAddress = entry.offset;
      }     
    }

    StackWalker::OnCallstackEntry( eType, entry );
  }

  virtual void OnOutput( LPCSTR szText ) 
  {
    //���� �����������������, �� ��������� ��������� �� �������, 
    //�.�. �������� ������������ GetSystemLog() 
    //DebugTrace( szText );
  }
  
private:
  void AppendAddressOnly( DWORD64 addr, const char *szFuncName ) const
  {     
    AddrToModName::const_iterator it = addrToModName.upper_bound( addr );
    SCallStackEntry &item = vec.push_back();
    
    item.dwAddress = addr;
    
    if( it != addrToModName.begin() )
    {
      --it; 
      
      //���� ������ �� �������, �� sprintf_s �������� invalid parameter handler ������� 
      //��������� ���������. OMG!
      _snprintf_s( item.szFunc.szStr, item.szFunc.N_STRING_CHARS, _TRUNCATE, "@rltv@ %s %I64X %s", it->second.c_str(), addr - it->first, szFuncName );  
    }       
  }

private:
  typedef map<DWORD64, string> AddrToModName;
                                            
private:
  CallStackVector &vec;
  AddrToModName addrToModName; 
};

inline bool IsNewStackWalkerEnabled()
{
  //AdaptedStackWalker �������� �� ���� ������������ ������� �,
  //� ������� �� ������ ��������, �������� � ��� Wine � ��� StarForce
  return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CollectCallStack( vector<SCallStackEntry> *pCallStack, const EXCEPTION_POINTERS *pExPtrs )
{
  if( !IsNewStackWalkerEnabled() )
  {
    pCallStack->clear();
	  pCallStack->resize( 128 );
    const int nEntries = LockSymEngine().CollectCallStack( &((*pCallStack)[0]), pCallStack->size(), 0, pExPtrs->ContextRecord );
    ReleaseSymEngine();
	  pCallStack->resize( nEntries );
	}
	else
	{
	  AdaptedStackWalker wlk( *pCallStack );  
	  wlk.ShowCallstack( GetCurrentThread(), pExPtrs->ContextRecord ); 
	}	
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CollectCallStack( vector<SCallStackEntry> *pCallStack )
{
  if( !IsNewStackWalkerEnabled() )
  {
    pCallStack->clear();
    pCallStack->resize( 128 );
    const int nEntries = LockSymEngine().CollectCallStack( &((*pCallStack)[0]), pCallStack->size(), 0 );
    ReleaseSymEngine();
    pCallStack->resize( nEntries );
  }
  else
	{
	  AdaptedStackWalker wlk( *pCallStack );  
	  wlk.ShowCallstack(); 
	} 
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#endif // NV_WIN_PLATFORM
