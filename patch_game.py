import sys

path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PW_Client/Game.cpp'
with open(path, 'rb') as f:
    lines = f.readlines()

new_lines = []
found_stdafx = False
for line in lines:
    if b'#include "stdafx.h"' in line and not found_stdafx:
        new_lines.append(line)
        new_lines.append(b'#ifdef _WIN32\n')
        found_stdafx = True
    else:
        new_lines.append(line)

if found_stdafx:
    new_lines.append(b'#else\n')
    new_lines.append(b'#include <stdlib.h>\n')
    new_lines.append(b'extern "C" {\n')
    new_lines.append(b'void StartPWApplication(void* hWnd) {}\n')
    new_lines.append(b'void StartPWPlugin(void* hWnd, int width, int height, bool fullscreen, const char* sessionLogin) {}\n')
    new_lines.append(b'}\n')
    new_lines.append(b'#endif\n')

with open(path, 'wb') as f:
    f.writelines(new_lines)
