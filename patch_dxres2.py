path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/Render/DxResourcesControl.cpp'
with open(path, 'rb') as f:
    content = f.read()

# Prepend #ifdef _WIN32 after stdafx.h
if b'#include "stdafx.h"' in content:
    content = content.replace(b'#include "stdafx.h"', b'#include "stdafx.h"\n#ifdef _WIN32')
    content += b'\n#endif // _WIN32'

with open(path, 'wb') as f:
    f.write(content)
