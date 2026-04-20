path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/Render/UITextureCache.h'
with open(path, 'rb') as f:
    content = f.read()

content = content.replace(b'template<> struct hash<flash::SWF_GRADIENT>', b'namespace nstl {\ntemplate<> struct hash<flash::SWF_GRADIENT>')
content = content.replace(b'      __h = 5 * __h + it->Color.a;\n    }\n\n    return (size_t)__h;\n  }\n};', b'      __h = 5 * __h + it->Color.a;\n    }\n\n    return (size_t)__h;\n  }\n};\n}')

with open(path, 'wb') as f:
    f.write(content)
