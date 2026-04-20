import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/DumpingStream.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

text = re.sub(
    r'(pData = \(StreamHeader \*\)VirtualAlloc\( 0, WinFileUnbuffered::GetPageSize\(\), MEM_COMMIT, PAGE_READWRITE \);)',
    r'#ifdef _WIN32\n  \1\n#else\n  pData = (StreamHeader *)malloc(WinFileUnbuffered::GetPageSize());\n#endif',
    text
)

text = re.sub(
    r'(#ifdef _WIN32\nVirtualFree\(pData, 0, MEM_RELEASE\)\n#else\nfree\(pData\)\n#endif;)',
    r'#ifdef _WIN32\n    VirtualFree(pData, 0, MEM_RELEASE);\n#else\n    free(pData);\n#endif',
    text
)

with open(path, "w", encoding="cp1251") as f:
    f.write(text)
