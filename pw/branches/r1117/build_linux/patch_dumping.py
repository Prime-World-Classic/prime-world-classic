import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/DumpingStream.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

text = re.sub(r'(VirtualAlloc\(NULL, (.*?), MEM_COMMIT, PAGE_READWRITE\))', r'#ifdef _WIN32\n\1\n#else\nmalloc(\2)\n#endif', text)
text = re.sub(r'(VirtualFree\((.*?), 0, MEM_RELEASE\))', r'#ifdef _WIN32\n\1\n#else\nfree(\2)\n#endif', text)

with open(path, "w", encoding="cp1251") as f:
    f.write(text)
