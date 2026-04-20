path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/Render/DxResourcesControl.cpp'
with open(path, 'rb') as f:
    lines = f.readlines()

new_lines = []
for line in lines:
    if b'#include "Vendor/DTW/inc/dbghelp.h"' in line:
        new_lines.append(b'#ifdef _WIN32\n' + line + b'#endif\n')
    elif b'#include "../MemoryLib/SymAccess.h"' in line:
        new_lines.append(b'#ifdef _WIN32\n' + line + b'#endif\n')
    else:
        new_lines.append(line)

# Also wrap function implementations that use stack walk
# We'll do it aggressively by wrapping large blocks if needed
# For now let's see if including shims in Win32_linux.h is enough

with open(path, 'wb') as f:
    f.writelines(new_lines)
