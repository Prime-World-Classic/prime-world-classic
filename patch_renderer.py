path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/Render/renderer.cpp'
with open(path, 'rb') as f:
    lines = f.readlines()

new_lines = []
for line in lines:
    if b'DDid.DriverVersion' in line:
        new_lines.append(b'#ifdef _WIN32\n' + line + b'#endif\n')
    elif b'EnumDisplaySettings' in line:
        new_lines.append(b'#ifdef _WIN32\n' + line + b'#endif\n')
    elif b'DEVMODE devmode;' in line:
        new_lines.append(b'#ifdef _WIN32\n' + line + b'#endif\n')
    elif b'Sleep(' in line:
        new_lines.append(line.replace(b'Sleep(', b'threading::Sleep('))
    else:
        new_lines.append(line)

with open(path, 'wb') as f:
    f.writelines(new_lines)
