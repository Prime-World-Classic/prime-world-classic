import re

path_rgen = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/RandomGen.cpp"
with open(path_rgen, "r", encoding="cp1251") as f:
    text_rgen = f.read()

text_rgen = text_rgen.replace('unsigned _int32', 'unsigned int')
text_rgen = text_rgen.replace('unsigned _int8', 'unsigned char')
text_rgen = text_rgen.replace('unsigned __int32', 'unsigned int')
text_rgen = text_rgen.replace('unsigned __int8', 'unsigned char')

with open(path_rgen, "w", encoding="cp1251") as f:
    f.write(text_rgen)


path_rttih = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/RTTI.h"
with open(path_rttih, "r", encoding="cp1251") as f:
    text_rttih = f.read()

text_rttih = text_rttih.replace('ti.raw_name()', '(ti.name())')

with open(path_rttih, "w", encoding="cp1251") as f:
    f.write(text_rttih)


path_rttic = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/RTTI.cpp"
with open(path_rttic, "r", encoding="cp1251") as f:
    text_rttic = f.read()

text_rttic = re.sub(r'strcpy_s\(([^,]+),\s*([^,]+),\s*([^)]+)\)', r'strncpy(\1, \3, \2)', text_rttic)

with open(path_rttic, "w", encoding="cp1251") as f:
    f.write(text_rttic)


path_pe = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/PersistEvents.cpp"
with open(path_pe, "r", encoding="cp1251") as f:
    text_pe = f.read()

text_pe = text_pe.replace('stricmp(', 'strcasecmp(')
text_pe = text_pe.replace('__time32_t', 'time_t')
text_pe = text_pe.replace('_time32(', 'time(')
text_pe = text_pe.replace('system/math/md4.h', 'System/Math/MD4.h')
text_pe = text_pe.replace('system/filesystem/fileutils.h', 'System/FileSystem/FileUtils.h')

text_pe = re.sub(r'(SetFileAttributesA?\([\s\S]*?\);)', r'#ifdef _WIN32\n\1\n#endif', text_pe)
text_pe = re.sub(r'DeleteFileA\( tmpName\.c_str\(\) \);', r'#ifdef _WIN32\n    DeleteFileA( tmpName.c_str() );\n#else\n    remove( tmpName.c_str() );\n#endif', text_pe)

with open(path_pe, "w", encoding="cp1251") as f:
    f.write(text_pe)
