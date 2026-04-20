import re

path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Win32_linux.h'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

# Add LPOVERLAPPED_COMPLETION_ROUTINE definition
text = text.replace('typedef void (CALLBACK *LPOVERLAPPED_COMPLETION_ROUTINE)(DWORD, DWORD, LPOVERLAPPED);', '') # remove if exists
text = text.replace('inline BOOL ReadFileEx(HANDLE, void*, DWORD, LPOVERLAPPED, void*) { return TRUE; }', 'typedef void (WINAPI *LPOVERLAPPED_COMPLETION_ROUTINE)(DWORD, DWORD, LPOVERLAPPED);\ninline BOOL WriteFileEx(HANDLE, const void*, DWORD, LPOVERLAPPED, LPOVERLAPPED_COMPLETION_ROUTINE) { return TRUE; }\ninline BOOL ReadFileEx(HANDLE, void*, DWORD, LPOVERLAPPED, LPOVERLAPPED_COMPLETION_ROUTINE) { return TRUE; }')

# Clean up duplicate WriteFileEx/ReadFileEx
text = text.replace('inline BOOL WriteFileEx(HANDLE, const void*, DWORD, LPOVERLAPPED, void*) { return TRUE; }', '')

with open(path, 'w', encoding='utf-8') as f:
    f.write(text)
