import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PW_Client/RegistryToolbox.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

# Wrap all content after stdafx.h
match = re.search(r'(#include "stdafx\.h"\s*\n)([\s\S]*)', text)
if match:
    stub = """
#ifdef _WIN32
""" + match.group(2) + """
#else
#include "RegistryToolbox.h"

namespace Game {
  bool GetRegString( HKEY root, const char* path, const char* valueName, nstl::string &retStr ) { return false; }
  void GetRegString( HKEY root, const char* path, const char* valueName, const char* defaultValue, nstl::string &retStr ) { retStr = defaultValue; }
}

#endif
"""
    with open(path, "w", encoding="cp1251") as f:
        f.write(match.group(1) + stub)
