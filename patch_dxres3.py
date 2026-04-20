path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/Render/DxResourcesControl.cpp'
with open(path, 'rb') as f:
    content = f.read()

stubs = b"""
#else // _WIN32
#include "DxResourcesControl.h"
static string s_dummyPool;
string const &GetCurrentDXPool() { return s_dummyPool; }
void PushDXPool(char const *pool) {}
void PopDXPool(char const *pool) {}
#endif // _WIN32
"""

if b'#endif // _WIN32' in content:
    content = content.replace(b'#endif // _WIN32', stubs)

with open(path, 'wb') as f:
    f.write(content)
