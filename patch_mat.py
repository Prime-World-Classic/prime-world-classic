path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/Render/Material.cpp'
with open(path, 'rb') as f:
    content = f.read()

content = content.replace(b'__asm {', b'#ifdef _WIN32\n    __asm {')
content = content.replace(b'mov eax, p\n    }', b'mov eax, p\n    }\n#endif')
content = content.replace(b'void MaterialManager::ResetSortIDs', b'void ResetSortIDs')

with open(path, 'wb') as f:
    f.write(content)
