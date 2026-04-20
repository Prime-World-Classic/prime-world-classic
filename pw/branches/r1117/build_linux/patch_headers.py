import re

def patch(file, patterns):
    with open(file, 'r', encoding='cp1251') as f:
        content = f.read()
    for p, repl in patterns:
        content = re.sub(p, repl, content)
    with open(file, 'w', encoding='cp1251') as f:
        f.write(content)

patch("/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/IniFiles.h", [
    (r'(nstl::string GetINIString\( LPCTSTR Section, LPCTSTR Key, LPCTSTR Default, LPCTSTR FileName \);)', r'#ifdef _WIN32\n\1\n#endif')
])

patch("/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/RegistryStorage.h", [
    (r'(#pragma once)', r'#pragma once\n#ifndef _WIN32\ntypedef void* HKEY;\ntypedef void** PHKEY;\ntypedef unsigned long DWORD;\n#endif')
])

patch("/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PW_Client/RegistryToolbox.h", [
    (r'(#pragma once)', r'#pragma once\n#ifndef _WIN32\ntypedef void* HKEY;\n#endif')
])
