path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/Render/TextureManager.cpp'
with open(path, 'r', encoding='cp1251') as f:
    text = f.read()

# Remove this->
text = text.replace('this->GetDefaultTexture', 'GetDefaultTexture')

# Find declarations and move them
decl1 = 'void GetDefaultTexture(Texture2DRef &pTex) { pTex = GetDefaultTexture2D(); }'
decl2 = 'void GetDefaultTexture(TextureCubeRef &pTex) { pTex = TextureCubeRef(); }'

text = text.replace(decl1, '')
text = text.replace(decl2, '')

# Add them before LoadTextureFromFile
insertion_point = 'template<class T> CObj<T> LoadTextureFromFile'
text = text.replace(insertion_point, decl1 + '\n' + decl2 + '\n\n' + insertion_point)

with open(path, 'w', encoding='cp1251') as f:
    f.write(text)
