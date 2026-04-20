path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Win32_linux.h'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

mb = """
inline int MessageBoxA(HWND, LPCSTR, LPCSTR, UINT) { return 0; }
inline int MessageBoxW(HWND, LPCWSTR, LPCWSTR, UINT) { return 0; }
#ifdef UNICODE
#define MessageBox MessageBoxW
#else
#define MessageBox MessageBoxA
#endif
"""

text = text.replace('inline DWORD GetCurrentThreadId() { return (DWORD)pthread_self(); }', 'inline DWORD GetCurrentThreadId() { return (DWORD)pthread_self(); }\n' + mb)

with open(path, 'w', encoding='utf-8') as f:
    f.write(text)
