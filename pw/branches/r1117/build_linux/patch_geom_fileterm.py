import re

geom_path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Geom.h"
with open(geom_path, "r", encoding="cp1251") as f:
    text = f.read()

text = re.sub(r'(  operator D3DXVECTOR3& \(\) \{ return reinterpret_cast<D3DXVECTOR3&>\(\*this\); \}\n  operator const D3DXVECTOR3& \(\) const \{ return reinterpret_cast<const D3DXVECTOR3&>\(\*this\); \})', r'#ifdef _WIN32\n\1\n#endif', text)

text = re.sub(r'(__forceinline D3DXVECTOR3\* AsD3D[\s\S]*?__forceinline const D3DXVECTOR3\* AsD3D\(const vector<const CVec3>& _src\) \{ return &static_cast<const D3DXVECTOR3&>\(_src\[0\]\); \})', r'#ifdef _WIN32\n\1\n#endif', text)

with open(geom_path, "w", encoding="cp1251") as f:
    f.write(text)

fileterm_path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/BlockData/src/FileTerminator.h"
with open(fileterm_path, "r", encoding="cp1251") as f:
    text = f.read()

text = re.sub(r'(#include "DataFlow\.h")', r'\1\n#ifndef _WIN32\n#include "../../Win32_linux.h"\n#endif', text)

with open(fileterm_path, "w", encoding="cp1251") as f:
    f.write(text)
