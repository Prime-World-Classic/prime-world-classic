path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/Render/UITextureCache.h'
with open(path, 'rb') as f:
    content = f.read()

content = content.replace(b'template<> struct hash<flash::SWF_GRADIENT>', b'namespace nstl {\ntemplate<> struct hash<flash::SWF_GRADIENT>')
# Find the end of the struct. It ends with }; followed by namespace flash or something.
# The previous replace was too fragile.
import re
content = re.sub(rb'(size_t operator\(\)\(const flash::SWF_GRADIENT& _gradient \) const[\s\S]*?\}\s*?\n\s*?\};)', b'\\1\n}', content)

with open(path, 'wb') as f:
    f.write(content)
