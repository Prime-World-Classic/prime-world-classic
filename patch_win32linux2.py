import re

path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Win32_linux.h'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

# Move GetCurrentThreadId inside extern "C"
text = re.sub(r'inline DWORD GetCurrentThreadId\(\) \{ return \(DWORD\)pthread_self\(\); \}', '', text)
text = text.replace('inline BOOL TerminateProcess(HANDLE, UINT) { return TRUE; }', 'inline BOOL TerminateProcess(HANDLE, UINT) { return TRUE; }\ninline DWORD GetCurrentThreadId() { return (DWORD)pthread_self(); }')

with open(path, 'w', encoding='utf-8') as f:
    f.write(text)
