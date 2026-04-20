import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PW_Client/Game.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

# 194: MessageBoxW
text = re.sub(
    r'(MessageBoxW\(NULL, .*?MB_TOPMOST\);)',
    r'#ifdef _WIN32\n\1\n#endif', text, flags=re.MULTILINE | re.DOTALL)

# 217-234: DumpLoadedModules (GetCurrentProcessId, MODULEENTRY32, CreateToolhelp32Snapshot, Module32First, Module32Next)
text = re.sub(
    r'(void DumpLoadedModules\(\)\n\{[\s\S]*?\n\})',
    r'#ifdef _WIN32\n\1\n#else\nvoid DumpLoadedModules() {}\n#endif', text)

# 271-288: CheckHardwareCompatibility (MEMORYSTATUSEX, GlobalMemoryStatusEx)
text = re.sub(
    r'(bool CheckHardwareCompatibility\(\)\n\{[\s\S]*?return true;\n\})',
    r'#ifdef _WIN32\n\1\n#else\nbool CheckHardwareCompatibility() { return true; }\n#endif', text)

# 444-459: _invalid_parameter_handler, DebugTraceInvalidParamsHandler, g_oldInvalidParamHandler
text = re.sub(
    r'(void DebugTraceInvalidParamsHandler[\s\S]*?g_oldInvalidParamHandler = _set_invalid_parameter_handler\(DebugTraceInvalidParamsHandler\);)',
    r'#ifdef _WIN32\n\1\n#endif', text)

text = re.sub(
    r'(_set_invalid_parameter_handler\(g_oldInvalidParamHandler\);)',
    r'#ifdef _WIN32\n\1\n#endif', text)

# 576: ShowWindow, TerminateProcess
text = re.sub(
    r'(ShowWindow\( hWnd, SW_MINIMIZE \);\n\s*TerminateProcess\( GetCurrentProcess\(\), 1 \);)',
    r'#ifdef _WIN32\n\1\n#else\nexit(1);\n#endif', text)

# 808-824: StartRemoteDebugger (LPTSTR, GetModuleFileName, CreateProcess)
text = re.sub(
    r'(void StartRemoteDebugger\(\)\n\{[\s\S]*?\n\})',
    r'#ifdef _WIN32\n\1\n#else\nvoid StartRemoteDebugger() {}\n#endif', text)

# 843-852: IsDebuggerPresent, MessageBox
text = re.sub(
    r'(if \(!IsDebuggerPresent\(\)\)\n\s*\{\n\s*MessageBox\([\s\S]*?\}\n\n\s*InitCommonControls\(\);)',
    r'#ifdef _WIN32\n\1\n#endif', text)

# 1135: MAKEINTRESOURCEW
text = re.sub(
    r'(MAKEINTRESOURCEW\(104\))',
    r'#ifdef _WIN32\n\1\n#else\n(const wchar_t*)104\n#endif', text)

# 1314: _countof(SERVER_IP_ARRAY) -> sizeof(SERVER_IP_ARRAY)/sizeof(SERVER_IP_ARRAY[0])
text = re.sub(
    r'_countof\(SERVER_IP_ARRAY\)',
    r'(sizeof(SERVER_IP_ARRAY)/sizeof(SERVER_IP_ARRAY[0]))', text)

# 1363-1373: CoInitialize, NBSU::SystemReport
text = re.sub(
    r'(CoInitialize\(NULL\);\n\n\s*NBSU::SystemReport sysRep;)',
    r'#ifdef _WIN32\n\1\n#endif', text)

# 1483-1490: CreateFileMapping, MapViewOfFile, GetWindowRect, UnmapViewOfFile
text = re.sub(
    r'(HANDLE hFileMapping = CreateFileMapping[\s\S]*?UnmapViewOfFile\(mappedData\);)',
    r'#ifdef _WIN32\n\1\n#endif', text)

# 1720-1725: StartPWApplication, StartPWPlugin
text = re.sub(
    r'(int __stdcall StartPWApplication\( void \)\n\{\n\s*return StartPWBase\(\);\n\}\n\nint __stdcall StartPWPlugin\( void \)\n\{\n\s*return StartPWBase\(\);\n\})',
    r'#ifdef _WIN32\n\1\n#else\nint StartPWApplication( void ) { return StartPWBase(); }\nint StartPWPlugin( void ) { return StartPWBase(); }\n#endif', text)

# 1788: cast from int* to int loses precision
text = re.sub(
    r'reinterpret_cast<int>\( \(&j\)\+1 \)',
    r'reinterpret_cast<intptr_t>( (&j)+1 )', text)

with open(path, "w", encoding="cp1251") as f:
    f.write(text)
