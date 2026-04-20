import re

# Patch ImageUnpackDXT.cpp
path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/ImageUnpackDXT.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

text = re.sub(r'\(DWORD\)(p|pColor|pComp|pSrc|pDst)', r'(size_t)\1', text)

with open(path, "w", encoding="cp1251") as f:
    f.write(text)

# Patch IniFiles.h
path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/IniFiles.h"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

text = re.sub(r'#ifdef _WIN32\n(.*?)\n#endif', r'\1', text) # Undo my previous bad patch

text = re.sub(r'#pragma once', r'#pragma once\n#ifndef _WIN32\n#ifndef LPCTSTR\ntypedef const char* LPCTSTR;\n#endif\n#endif', text)

with open(path, "w", encoding="cp1251") as f:
    f.write(text)

# Patch IniFiles.cpp
path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/IniFiles.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

# Wrap implementations
text = re.sub(r'(int GetINIInt\( LPCTSTR Section, LPCTSTR Key, int Default, LPCTSTR FileName \)\n\{[\s\S]*?\n\})',
r'#ifdef _WIN32\n\1\n#else\nint GetINIInt( LPCTSTR Section, LPCTSTR Key, int Default, LPCTSTR FileName ) { return Default; }\n#endif', text)

text = re.sub(r'(void GetINIString\( LPCTSTR Section, LPCTSTR Key, LPCTSTR Default, nstl::string &retStr, LPCTSTR FileName \)\n\{[\s\S]*?\n\})',
r'#ifdef _WIN32\n\1\n#else\nvoid GetINIString( LPCTSTR Section, LPCTSTR Key, LPCTSTR Default, nstl::string &retStr, LPCTSTR FileName ) { retStr = Default ? Default : ""; }\n#endif', text)

text = re.sub(r'(nstl::string GetINIString\( LPCTSTR Section, LPCTSTR Key, LPCTSTR Default, LPCTSTR FileName \)\n\{[\s\S]*?\n\})',
r'#ifdef _WIN32\n\1\n#else\nnstl::string GetINIString( LPCTSTR Section, LPCTSTR Key, LPCTSTR Default, LPCTSTR FileName ) { return Default ? Default : ""; }\n#endif', text)

text = re.sub(r'(void GetINIStringW\( LPCTSTR Section, LPCTSTR Key, LPCTSTR Default, wstring &retStr, LPCTSTR FileName \)\n\{[\s\S]*?\n\})',
r'#ifdef _WIN32\n\1\n#else\nvoid GetINIStringW( LPCTSTR Section, LPCTSTR Key, LPCTSTR Default, wstring &retStr, LPCTSTR FileName ) { retStr = L""; }\n#endif', text)

text = re.sub(r'(float GetINIFloat\( LPCTSTR Section, LPCTSTR Key, float Default, LPCTSTR FileName \)\n\{[\s\S]*?\n\})',
r'#ifdef _WIN32\n\1\n#else\nfloat GetINIFloat( LPCTSTR Section, LPCTSTR Key, float Default, LPCTSTR FileName ) { return Default; }\n#endif', text)

text = re.sub(r'(void WriteINIString\( LPCTSTR Section, LPCTSTR Key, LPCTSTR Value, LPCTSTR FileName \)\n\{[\s\S]*?\n\})',
r'#ifdef _WIN32\n\1\n#else\nvoid WriteINIString( LPCTSTR Section, LPCTSTR Key, LPCTSTR Value, LPCTSTR FileName ) {}\n#endif', text)

with open(path, "w", encoding="cp1251") as f:
    f.write(text)

