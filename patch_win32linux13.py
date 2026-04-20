path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Win32_linux.h'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

mb = """
#define MB_ICONSTOP                 0x00000010L
#define MB_TASKMODAL                0x00002000L
#define MB_SERVICE_NOTIFICATION     0x00200000L
"""

text = text.replace('#define MB_TOPMOST                  0x00040000L', '#define MB_TOPMOST                  0x00040000L\n' + mb)

with open(path, 'w', encoding='utf-8') as f:
    f.write(text)
