path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Win32_linux.h'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

text = text.replace('typedef uintptr_t     ULONG_PTR;', 'typedef uintptr_t     ULONG_PTR;\ntypedef uintptr_t     DWORD_PTR;')

# We need to hack D3DADAPTER_IDENTIFIER9 before including d3d9types.h or after
# Actually d3d9types.h defines it. We can redefine it or wrap the usage.
# Better wrap usage in renderer.cpp

with open(path, 'w', encoding='utf-8') as f:
    f.write(text)
