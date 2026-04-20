import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/RTTI.h"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

text = text.replace('ti.raw_name()', '(ti.name())')
with open(path, "w", encoding="cp1251") as f:
    f.write(text)

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/RandomGen.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

text = text.replace('unsigned __int32', 'unsigned int')
text = text.replace('unsigned __int64', 'unsigned long long')
text = text.replace('__int32', 'int')
text = text.replace('__int64', 'long long')

with open(path, "w", encoding="cp1251") as f:
    f.write(text)

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/RTTI.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()
text = text.replace('strcpy_s(className, sizeof(className), ti.raw_name());', 'strncpy(className, ti.name(), sizeof(className));')
text = text.replace('strcpy_s(className, sizeof(className), ti.name());', 'strncpy(className, ti.name(), sizeof(className));')
with open(path, "w", encoding="cp1251") as f:
    f.write(text)

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/PersistEvents.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

text = text.replace('stricmp(', 'strcasecmp(')
text = text.replace('__time32_t', 'time_t')
text = text.replace('_time32(', 'time(')

text = re.sub(r'(SetFileAttributesA?\([\s\S]*?\);)', r'#ifdef _WIN32\n\1\n#endif', text)
text = re.sub(r'(DeleteFileA?\([\s\S]*?\);)', r'#ifdef _WIN32\n\1\n#else\nremove( \2 );\n#endif', text)  # \2 isn't strictly defined in my regex, wait. I will fix DeleteFile manually.

with open(path, "w", encoding="cp1251") as f:
    f.write(text)

