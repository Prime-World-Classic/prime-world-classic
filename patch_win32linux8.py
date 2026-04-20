path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Win32_linux.h'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

text = text.replace('typedef int64_t       __int64;', '#define __int64 long long')
text = text.replace('typedef int32_t       __int32;', '#define __int32 int')
text = text.replace('typedef int16_t       __int16;', '#define __int16 short')
text = text.replace('typedef int8_t        __int8;', '#define __int8 char')

with open(path, 'w', encoding='utf-8') as f:
    f.write(text)
