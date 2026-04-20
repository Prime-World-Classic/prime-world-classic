import re

path_h = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/IniFiles.h"
with open(path_h, "r", encoding="cp1251") as f:
    text_h = f.read()

text_h = re.sub(
    r'(#define _INI_FILES_H_\n)',
    r'\1\n#ifndef _WIN32\ntypedef const char* LPCTSTR;\ntypedef const wchar_t* LPCWSTR;\n#endif\n',
    text_h
)

with open(path_h, "w", encoding="cp1251") as f:
    f.write(text_h)

path_cpp = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/IniFiles.cpp"
with open(path_cpp, "r", encoding="cp1251") as f:
    text_cpp = f.read()

text_cpp = re.sub(
    r'(void GetINIString\(string \* dest, LPCTSTR pszDir, LPCTSTR pszFile, LPCTSTR pszSection, LPCTSTR pszName\)\n\{)([\s\S]*?)(\n\})',
    r'\1#ifdef _WIN32\2#else\n  if (dest) *dest = "";\n#endif\3',
    text_cpp
)

text_cpp = re.sub(
    r'(void GetINIString\(wstring \* dest, LPCWSTR pszDir, LPCWSTR pszFile, LPCWSTR pszSection, LPCWSTR pszName\)\n\{)([\s\S]*?)(\n\})',
    r'\1#ifdef _WIN32\2#else\n  if (dest) *dest = L"";\n#endif\3',
    text_cpp
)

with open(path_cpp, "w", encoding="cp1251") as f:
    f.write(text_cpp)
