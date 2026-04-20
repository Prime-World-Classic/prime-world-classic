path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Win32_linux.h'
with open(path, 'r', encoding='utf-8') as f:
    lines = f.readlines()

new_lines = []
skip = False
for line in lines:
    if '#ifndef OutputDebugStringA' in line:
        skip = True
        continue
    if skip:
        if '#endif' in line:
            # We found the end of the block, but wait, there might be nested ones.
            # In our case, it's simple.
            skip = False
        continue
    new_lines.append(line)

# Add it once properly
final_lines = []
for line in new_lines:
    if 'inline void SleepEx(DWORD, BOOL) {}' in line:
        final_lines.append(line)
        final_lines.append('\n')
        final_lines.append('#ifdef __cplusplus\n')
        final_lines.append('inline void OutputDebugStringA(const char*) {}\n')
        final_lines.append('inline void OutputDebugStringW(const wchar_t*) {}\n')
        final_lines.append('#else\n')
        final_lines.append('#define OutputDebugStringA(x) ((void)0)\n')
        final_lines.append('#define OutputDebugStringW(x) ((void)0)\n')
        final_lines.append('#endif\n')
        final_lines.append('#define OutputDebugString OutputDebugStringA\n')
    else:
        final_lines.append(line)

with open(path, 'w', encoding='utf-8') as f:
    f.writelines(final_lines)
