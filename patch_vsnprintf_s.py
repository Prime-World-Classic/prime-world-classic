path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Win32_linux.h'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

text = text.replace('#define vsnprintf_s(buffer, sizeOfBuffer, count, format, args) vsnprintf(buffer, count == (size_t)-1 ? sizeOfBuffer : (count > sizeOfBuffer ? sizeOfBuffer : count), format, args)', '') # cleanup
text = text.replace('#define _vsnprintf_s vsnprintf_s', '') # cleanup

defs = """
typedef char* PSTR;
typedef unsigned char* PUCHAR;
typedef int BOOLEAN;
typedef struct _LIST_ENTRY {
   struct _LIST_ENTRY *Flink;
   struct _LIST_ENTRY *Blink;
} LIST_ENTRY;
typedef uint64_t DWORD64;
typedef uint64_t ULONG64;
typedef uint32_t ULONG32;
typedef uint32_t RVA;
typedef uint64_t RVA64;

#define _malloca malloc
#define _freea free
#define EXCEPTION_BREAKPOINT 0x80000003L
#define _vsnprintf_s vsnprintf_s
"""

text = text.replace('#include "System/types.h"', '#include "System/types.h"\n' + defs)

with open(path, 'w', encoding='utf-8') as f:
    f.write(text)
