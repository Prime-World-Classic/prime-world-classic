import re

path_rttih = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/RTTI.h"
with open(path_rttih, "r", encoding="cp1251") as f:
    text_rttih = f.read()

text_rttih = text_rttih.replace('ti.raw_name()', 'ti.name()')
with open(path_rttih, "w", encoding="cp1251") as f:
    f.write(text_rttih)

path_rttic = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/RTTI.cpp"
with open(path_rttic, "r", encoding="cp1251") as f:
    text_rttic = f.read()

text_rttic = re.sub(r'strcpy_s\(([^,]+),\s*sizeof\([^)]+\),\s*(.*?)\);', r'strncpy(\1, \2, sizeof(\1));', text_rttic)

with open(path_rttic, "w", encoding="cp1251") as f:
    f.write(text_rttic)

path_pe = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/PersistEvents.cpp"
with open(path_pe, "r", encoding="cp1251") as f:
    text_pe = f.read()

text_pe = re.sub(r'DeleteFileA\((.*?)\);', r'remove(\1);', text_pe)
with open(path_pe, "w", encoding="cp1251") as f:
    f.write(text_pe)

path_rsh = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/RegistryStorage.h"
with open(path_rsh, "r", encoding="cp1251") as f:
    text_rsh = f.read()

text_rsh = re.sub(r'#pragma once', r'#pragma once\n#ifndef _WIN32\ntypedef void* HKEY;\ntypedef void** PHKEY;\ntypedef unsigned long DWORD;\n#endif', text_rsh)
text_rsh = text_rsh.replace('string ', 'nstl::string ')
text_rsh = text_rsh.replace('vector<', 'nstl::vector<')

with open(path_rsh, "w", encoding="cp1251") as f:
    f.write(text_rsh)

path_rsc = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/RegistryStorage.cpp"
with open(path_rsc, "r", encoding="cp1251") as f:
    text_rsc = f.read()

match = re.search(r'(#include "RegistryStorage\.h"\s*\n)([\s\S]*)', text_rsc)
if match:
    # remove my previous stub
    content = match.group(2)
    content = re.sub(r'#ifdef _WIN32\n(.*?)#else[\s\S]*#endif', r'\1', content, flags=re.DOTALL)
    
    stub = """#ifdef _WIN32
""" + content + """
#else

namespace registry {
  Storage::Storage(const nstl::string& company, const nstl::string& app) {}
  Storage::~Storage() {}

  void Storage::Write(const nstl::string& key, int val) {}
  void Storage::Write(const nstl::string& key, const nstl::string& val) {}
  void Storage::Write(const nstl::string& key, const void* val, int len) {}

  int Storage::Read(const nstl::string& key, int def) { return def; }
  nstl::string Storage::Read(const nstl::string& key, const nstl::string& def) { return def; }
  void Storage::Read(const nstl::string& key, void* val, int len) {}

  void Storage::OpenKey(HKEY root, const nstl::string& path, PHKEY key) {}
  void Storage::CloseKey(PHKEY key) {}
}

#endif
"""
    with open(path_rsc, "w", encoding="cp1251") as f:
        f.write(match.group(1) + stub)

