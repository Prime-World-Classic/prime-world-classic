import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/MainFrame.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

match = re.search(r'(#include "MainFrame\.h"\s*\n)([\s\S]*)', text)
if match:
    stub = """
#ifdef _WIN32
""" + match.group(2) + """
#else

namespace NMainFrame {
  bool GetMessage( SWindowsMsg *pRes ) { return false; }
  bool IsAppActive() { return true; }
  bool IsAppNotMinimized() { return true; }
  bool IsExit() { return false; }
  void Exit() { exit(0); }
  void Exit( const nstl::string& exitCode ) { exit(0); }
  void SetExitCode( const nstl::string& exitCode ) {}
  const nstl::string& GetExitCode() { static nstl::string empty; return empty; }
  HWND GetWnd() { return NULL; }
  void SetWnd(HWND _hwnd) {}
  HINSTANCE GetInstance() { return NULL; }
  void PumpMessages() {}
  bool InitApplication( HINSTANCE hInstance, const char *pszAppName, const char *pszWndName, LPCWSTR nIcon, bool fullscreen, int width, int height, HWND hUseWindow ) { return true; }
  void ShutdownApplication() {}
  void SetCursor( HCURSOR _hCursor ) {}
  void ShowCursor( bool bShow ) {}
  HCURSOR GetCurrentCursor() { return NULL; }
  void EnableCursorManagement( bool bEnable ) {}
  bool UpdateCursorRectInt( const char* name, const vector<wstring> &paramsSet ) { return true; }
  void ResizeWindow( unsigned long width, unsigned long height, bool isFullScreen, bool isBorderless ) {}
  void DumpWindowStyle( DWORD dwStyle ) {}
  void DumpExWindowStyle( DWORD dwStyle ) {}
  void ApplyNewParams( unsigned long width, unsigned long height, bool isFullScreen, bool isBorderless ) {}
  void SetActualClipCursorRect() {}
  void SetCloseHandler( ICloseApplicationHandler* handler ) {}
}

void SetStepCallback( void (*_stepFunc)() ) {}

#endif
"""
    with open(path, "w", encoding="cp1251") as f:
        f.write(match.group(1) + stub)

