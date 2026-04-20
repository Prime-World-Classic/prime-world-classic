import re

path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/Render/TextureManager.cpp'
with open(path, 'r', encoding='cp1251') as f:
    text = f.read()

# Remove this->
text = text.replace('this->GetDefaultTexture', 'GetDefaultTexture')

# Find declarations and move them
# Note: they might be missing if I already moved them but they are in the wrong place
decl1 = 'void GetDefaultTexture(Texture2DRef &pTex) { pTex = GetDefaultTexture2D(); }'
decl2 = 'void GetDefaultTexture(TextureCubeRef &pTex) { pTex = TextureCubeRef(); }'

text = text.replace(decl1, '')
text = text.replace(decl2, '')

# Add them at the beginning of the Render namespace
insertion_point = 'namespace Render\n{'
text = text.replace(insertion_point, insertion_point + '\n' + decl1 + '\n' + decl2 + '\n')

with open(path, 'w', encoding='cp1251') as f:
    f.write(text)
