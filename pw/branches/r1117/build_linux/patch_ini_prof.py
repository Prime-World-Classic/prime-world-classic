import re

path_ini = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/IniFiles.cpp"
with open(path_ini, "r", encoding="cp1251") as f:
    text_ini = f.read()

text_ini = re.sub(
    r'(void GetINIString\(string \* dest, LPCTSTR pszDir, LPCTSTR pszFile, LPCTSTR pszSection, LPCTSTR pszName\)\n\{)([\s\S]*?)(\n\})',
    '\\1\n#ifdef _WIN32\n\\2\n#else\n  if (dest) *dest = "";\n#endif\\3',
    text_ini
)

text_ini = re.sub(
    r'(void GetINIString\(wstring \* dest, LPCWSTR pszDir, LPCWSTR pszFile, LPCWSTR pszSection, LPCWSTR pszName\)\n\{)([\s\S]*?)(\n\})',
    '\\1\n#ifdef _WIN32\n\\2\n#else\n  if (dest) *dest = L"";\n#endif\\3',
    text_ini
)

text_ini = re.sub(
    r'(nstl::string GetINIString\(LPCTSTR pszDir, LPCTSTR pszFile, LPCTSTR pszSection, LPCTSTR pszName\)\n\{)([\s\S]*?)(\n\})',
    '\\1\n#ifdef _WIN32\n\\2\n#else\n  return "";\n#endif\\3',
    text_ini
)

text_ini = re.sub(
    r'(wstring GetINIString\(LPCWSTR pszDir, LPCWSTR pszFile, LPCWSTR pszSection, LPCWSTR pszName\)\n\{)([\s\S]*?)(\n\})',
    '\\1\n#ifdef _WIN32\n\\2\n#else\n  return L"";\n#endif\\3',
    text_ini
)

text_ini = re.sub(
    r'(int GetINIInt\( LPCTSTR Section, LPCTSTR Key, int Default, LPCTSTR FileName \)\n\{)([\s\S]*?)(\n\})',
    '\\1\n#ifdef _WIN32\n\\2\n#else\n  return Default;\n#endif\\3',
    text_ini
)

text_ini = re.sub(
    r'(float GetINIFloat\( LPCTSTR Section, LPCTSTR Key, float Default, LPCTSTR FileName \)\n\{)([\s\S]*?)(\n\})',
    '\\1\n#ifdef _WIN32\n\\2\n#else\n  return Default;\n#endif\\3',
    text_ini
)

text_ini = re.sub(
    r'(void WriteINIString\( LPCTSTR Section, LPCTSTR Key, LPCTSTR Value, LPCTSTR FileName \)\n\{)([\s\S]*?)(\n\})',
    '\\1\n#ifdef _WIN32\n\\2\n#else\n  return;\n#endif\\3',
    text_ini
)

with open(path_ini, "w", encoding="cp1251") as f:
    f.write(text_ini)


path_prof = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/InlineProfiler3/Profiler3UIData.cpp"
with open(path_prof, "r", encoding="cp1251") as f:
    text_prof = f.read()

# Profiler3UIData.cpp handles OS specific stats.
text_prof = re.sub(
    r'(__int64 SystemThreadTime\( unsigned long threadId \)\n\{)([\s\S]*?)(\n\})',
    '\\1\n#ifdef _WIN32\n\\2\n#else\n  return 0;\n#endif\\3',
    text_prof
)

text_prof = re.sub(
    r'(unsigned long SystemMemoryUsage\(\)\n\{)([\s\S]*?)(\n\})',
    '\\1\n#ifdef _WIN32\n\\2\n#else\n  return 0;\n#endif\\3',
    text_prof
)

with open(path_prof, "w", encoding="cp1251") as f:
    f.write(text_prof)

