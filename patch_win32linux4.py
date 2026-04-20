import re

path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Win32_linux.h'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

# Add more definitions
defs = """
#define _I64_MAX LLONG_MAX
#define _I64_MIN LLONG_MIN
#define _I32_MAX INT_MAX
#define _I32_MIN INT_MIN

inline HRESULT UuidCreate(GUID* p) { memset(p, 0, sizeof(GUID)); return S_OK; }
inline BOOL IsEqualGUID(REFGUID rguid1, REFGUID rguid2) { return memcmp(&rguid1, &rguid2, sizeof(GUID)) == 0; }
"""

text = text.replace('#include <sys/types.h>', '#include <sys/types.h>\n' + defs)

with open(path, 'w', encoding='utf-8') as f:
    f.write(text)
