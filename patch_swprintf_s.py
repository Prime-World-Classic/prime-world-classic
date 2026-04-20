path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Win32_linux.h'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

import re
text = re.sub(r'template <size_t size>\s*inline int swprintf_s[\s\S]*?va_end\(args\);\s*return ret;\s*\}', '', text)

swprintf_s = """
#ifdef __cplusplus
template <size_t size>
inline int swprintf_s(wchar_t (&buffer)[size], const wchar_t *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vswprintf(buffer, size, format, args);
    va_end(args);
    return ret;
}
inline int swprintf_s(wchar_t *buffer, size_t sizeOfBuffer, const wchar_t *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vswprintf(buffer, sizeOfBuffer, format, args);
    va_end(args);
    return ret;
}
#endif
"""

text = text.replace('#define swprintf_s swprintf', swprintf_s)

with open(path, 'w', encoding='utf-8') as f:
    f.write(text)
