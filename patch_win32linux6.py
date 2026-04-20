path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Win32_linux.h'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

import re
text = re.sub(r'#ifdef __cplusplus\s*template <size_t size>\s*inline int strcpy_s[\s\S]*?#define strcpy_s\(dest, size, src\) \(strncpy\(dest, src, size\), 0\)', '', text)

strcpy_s = """
#ifdef __cplusplus
template <size_t size>
inline int strcpy_s(char (&buffer)[size], const char *src) {
    strncpy(buffer, src, size);
    return 0;
}
inline int strcpy_s(char *dest, size_t size, const char *src) {
    strncpy(dest, src, size);
    return 0;
}
#else
#define strcpy_s(dest, size, src) (strncpy(dest, src, size), 0)
#endif
"""

text = text.replace('inline int sprintf_s(char *buffer, size_t sizeOfBuffer, const char *format, ...) {', strcpy_s + '\ninline int sprintf_s(char *buffer, size_t sizeOfBuffer, const char *format, ...) {')

with open(path, 'w', encoding='utf-8') as f:
    f.write(text)
