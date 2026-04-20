path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/systemStdAfx.h'
with open(path, 'rb') as f:
    content = f.read()

content = content.replace(b'typedef long LONG;', b'// typedef long LONG; // Handled in Win32_linux.h')
content = content.replace(b'using namespace nstl;', b'// using namespace nstl; // Avoid ambiguity')

with open(path, 'wb') as f:
    f.write(content)
