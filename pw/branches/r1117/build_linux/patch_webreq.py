import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/Shared/WebRequests.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

text = re.sub(r'#endif\n\n\nstd::string GetSessionData', r'#ifndef _WIN32\n#define _snprintf_s(buf, size, fmt, ...) snprintf(buf, size, fmt, __VA_ARGS__)\n#define _countof(a) (sizeof(a)/sizeof(*(a)))\n#endif\n\n\n#endif\n\nstd::string GetSessionData', text)

with open(path, "w", encoding="cp1251") as f:
    f.write(text)
