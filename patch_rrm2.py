path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/Render/renderresourcemanager.cpp'
with open(path, 'rb') as f:
    content = f.read()

# Make GeometryPool protected instead of private
content = content.replace(b'class BasicPoolFinder\n{', b'class BasicPoolFinder\n{\nprotected:')

with open(path, 'wb') as f:
    f.write(content)
