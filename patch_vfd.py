path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/Render/vertexformatdescriptor.cpp'
with open(path, 'rb') as f:
    lines = f.readlines()

new_lines = []
skip = False
for line in lines:
    if b'VertexFormatDescriptor::VertexFormatDescriptor()' in line:
        skip = True
        continue
    if skip and b'}' in line:
        skip = False
        continue
    if not skip:
        new_lines.append(line)

with open(path, 'wb') as f:
    f.writelines(new_lines)
