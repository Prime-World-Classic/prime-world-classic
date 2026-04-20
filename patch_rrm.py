path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/Render/renderresourcemanager.cpp'
with open(path, 'rb') as f:
    content = f.read()

# Fix dependent names
content = content.replace(b' Find(filename)', b' this->Find(filename)')
content = content.replace(b' MarkAsLoaded(result, filename)', b' this->MarkAsLoaded(result, filename)')

# Fix private access in BasicPoolFinder
content = content.replace(b'class BasicPoolFinder\n{\nprivate:', b'class BasicPoolFinder\n{\npublic:')

with open(path, 'wb') as f:
    f.write(content)
