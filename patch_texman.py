path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/Render/TextureManager.cpp'
with open(path, 'rb') as f:
    lines = f.readlines()

new_lines = []
in_template = False
for line in lines:
    if b'template' in line:
        in_template = True
    if b'}' in line and in_template and line.startswith(b'}'):
        in_template = False
    
    if in_template:
        line = line.replace(b'GetDefaultTexture', b'this->GetDefaultTexture')
    new_lines.append(line)

with open(path, 'wb') as f:
    f.writelines(new_lines)
