#ifndef _INI_FILES_H_
#define _INI_FILES_H_

#ifndef _WIN32
typedef const char* LPCTSTR;
typedef const wchar_t* LPCWSTR;
#endif

namespace NIniFiles
{

  void GetINIString(string * dest, LPCTSTR pszDir, LPCTSTR pszFile, LPCTSTR pszSection, LPCTSTR pszName);
  void GetINIString(wstring * dest, LPCWSTR pszDir, LPCWSTR pszFile, LPCWSTR pszSection, LPCWSTR pszName);
  string GetINIString(LPCTSTR pszDir, LPCTSTR pszFile, LPCTSTR pszSection, LPCTSTR pszName);
  wstring GetINIString(LPCWSTR pszDir, LPCWSTR pszFile, LPCWSTR pszSection, LPCWSTR pszName);

} // namespace NIniFiles

#endif // _INI_FILES_H_