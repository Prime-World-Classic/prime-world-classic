path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/SyncProcessorState.cpp'
with open(path, 'rb') as f:
    lines = f.readlines()

new_lines = []
for line in lines:
    if b'#include <psapi.h>' in line or b'#include <tlhelp32.h>' in line:
        new_lines.append(b'#ifdef _WIN32\n' + line + b'#endif\n')
    elif b'unsigned int GetProcessorState()' in line:
        new_lines.append(b'#ifdef _WIN32\n' + line)
    elif b'  return _control87( 0, 0 );' in line:
        new_lines.append(line + b'#else\n  return 0;\n#endif\n')
    elif b'void SetProcessorState(' in line:
        new_lines.append(b'#ifdef _WIN32\n' + line)
    elif b'  _control87( _state, _mask );' in line:
        new_lines.append(line + b'#endif\n')
    else:
        new_lines.append(line)

with open(path, 'wb') as f:
    f.writelines(new_lines)
