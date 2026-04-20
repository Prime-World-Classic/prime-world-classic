path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Win32_linux.h'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

strcpy_s = """
#ifdef __cplusplus
template <size_t size>
inline int strcpy_s(char (&buffer)[size], const char *src) {
    strncpy(buffer, src, size);
    return 0;
}
#endif
#define strcpy_s(dest, size, src) (strncpy(dest, src, size), 0)
"""

text = text.replace('inline int sprintf_s(char *buffer, size_t sizeOfBuffer, const char *format, ...) {', strcpy_s + '\ninline int sprintf_s(char *buffer, size_t sizeOfBuffer, const char *format, ...) {')

with open(path, 'w', encoding='utf-8') as f:
    f.write(text)
