import re
import os

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PW_Client/Game.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

# _invalid_parameter_handler and DebugTraceInvalidParamsHandler
text = re.sub(
    r'(void DebugTraceInvalidParamsHandler[\s\S]*?_set_invalid_parameter_handler\(DebugTraceInvalidParamsHandler\);)',
    r'#ifdef _WIN32\n\1\n#endif', text)

text = re.sub(
    r'(_set_invalid_parameter_handler\(g_oldInvalidParamHandler\);)',
    r'#ifdef _WIN32\n\1\n#endif', text)

# IsDebuggerPresent and MessageBox
text = re.sub(
    r'(if \(!IsDebuggerPresent\(\)\)[\s\S]*?InitCommonControls\(\);)',
    r'#ifdef _WIN32\n\1\n#endif', text)

# StartRemoteDebugger
text = re.sub(
    r'(void StartRemoteDebugger\(\)\n\{[\s\S]*?\n\})',
    r'#ifdef _WIN32\n\1\n#else\nvoid StartRemoteDebugger() {}\n#endif', text)

# MessageBoxW in HandleMessage
text = re.sub(
    r'(MessageBoxW\(NULL, [^\)]+MB_TOPMOST\);)',
    r'#ifdef _WIN32\n\1\n#endif', text, flags=re.DOTALL)

# GetCurrentProcessId, DumpLoadedModules
text = re.sub(
    r'(void DumpLoadedModules\(\)\n\{[\s\S]*?\n\})',
    r'#ifdef _WIN32\n\1\n#else\nvoid DumpLoadedModules() {}\n#endif', text)

# CheckHardwareCompatibility
text = re.sub(
    r'(bool CheckHardwareCompatibility\(\)\n\{[\s\S]*?\n\})',
    r'#ifdef _WIN32\n\1\n#else\nbool CheckHardwareCompatibility() { return true; }\n#endif', text)

# ShowWindow and TerminateProcess
text = re.sub(
    r'(ShowWindow\(\s*hWnd,\s*SW_MINIMIZE\s*\);\n\s*TerminateProcess\(\s*GetCurrentProcess\(\),\s*1\s*\);)',
    r'#ifdef _WIN32\n\1\n#else\nexit(1);\n#endif', text)

# StartPWApplication and StartPWPlugin
text = re.sub(
    r'(int __stdcall StartPWApplication\( void \)\n\{\n\s*return StartPWBase\(\);\n\}\n\nint __stdcall StartPWPlugin\( void \)\n\{\n\s*return StartPWBase\(\);\n\})',
    r'#ifdef _WIN32\n\1\n#else\nint StartPWApplication( void ) { return StartPWBase(); }\nint StartPWPlugin( void ) { return StartPWBase(); }\n#endif', text)

# CreateFileMapping
text = re.sub(
    r'(HANDLE hFileMapping = CreateFileMapping[\s\S]*?UnmapViewOfFile\(mappedData\);)',
    r'#ifdef _WIN32\n\1\n#endif', text)

# CoInitialize and NBSU::SystemReport
text = re.sub(
    r'(CoInitialize\(NULL\);\n\s*NBSU::SystemReport sysRep;)',
    r'#ifdef _WIN32\n\1\n#endif', text)

# MAKEINTRESOURCEW
text = re.sub(
    r'(MAKEINTRESOURCEW\(104\))',
    r'#ifdef _WIN32\n\1\n#else\n(const wchar_t*)104\n#endif', text)

# SERVER_IP_ARRAY
text = re.sub(
    r'_countof\(SERVER_IP_ARRAY\)',
    r'(sizeof(SERVER_IP_ARRAY)/sizeof(SERVER_IP_ARRAY[0]))', text)

# int ptr cast
text = re.sub(
    r'reinterpret_cast<int>\( \(&j\)\+1 \)',
    r'reinterpret_cast<intptr_t>( (&j)+1 )', text)

with open(path, "w", encoding="cp1251") as f:
    f.write(text)
