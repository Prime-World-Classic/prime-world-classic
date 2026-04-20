path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Win32_linux.h'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

fopen_s = """
inline int fopen_s(FILE** pFile, const char* filename, const char* mode) {
    *pFile = fopen(filename, mode);
    return *pFile ? 0 : errno;
}
"""

text = text.replace('inline int sprintf_s(char *buffer, size_t sizeOfBuffer, const char *format, ...) {', fopen_s + '\ninline int sprintf_s(char *buffer, size_t sizeOfBuffer, const char *format, ...) {')

with open(path, 'w', encoding='utf-8') as f:
    f.write(text)
