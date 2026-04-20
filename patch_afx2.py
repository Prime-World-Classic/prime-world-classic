path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/systemStdAfx.h'
with open(path, 'rb') as f:
    content = f.read()

# Restore using namespace nstl
content = content.replace(b'// using namespace nstl; // Avoid ambiguity', b'using namespace nstl;')

with open(path, 'wb') as f:
    f.write(content)
