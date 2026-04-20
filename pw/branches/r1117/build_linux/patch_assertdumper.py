import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/AssertDumper.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

text = re.sub(r'(void ThrowExceptionAndDump\(\)\n\{[\s\S]*?\n\})', r'#ifdef _WIN32\n\1\n#else\n#include <stdlib.h>\nvoid ThrowExceptionAndDump() { abort(); }\n#endif', text)

text = re.sub(r'__debugbreak\(\);', r'#ifdef _WIN32\n    __debugbreak();\n#else\n    abort();\n#endif', text)
text = re.sub(r'FatalExit\( -1 \);', r'#ifdef _WIN32\n    FatalExit( -1 );\n#else\n    exit( -1 );\n#endif', text)

with open(path, "w", encoding="cp1251") as f:
    f.write(text)
