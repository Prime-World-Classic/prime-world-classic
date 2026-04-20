import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PW_Client/Game.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

text = re.sub(
    r'(#include <windows\.h>\n#include <TlHelp32\.h>\n\nint NumProcessRunning\(const char\* processName\)\n\{\n[\s\S]*?return count;\n\})',
    r'#ifdef _WIN32\n\1\n#else\nint NumProcessRunning(const char* processName) { return 0; }\n#endif',
    text
)

text = re.sub(
    r'(static void RunLinuxLauncher\(\) \{\n[\s\S]*?systemLog\( NLogg::LEVEL_MESSAGE \) << "Linux proc run: \\"\" << procRun << "\\"" << endl;\n\})',
    r'#ifdef _WIN32\n\1\n#else\n#include <stdlib.h>\nstatic void RunLinuxLauncher() { system("nohup ../../Launcher/PWClassic > /dev/null 2>&1 &"); }\n#endif',
    text
)

with open(path, "w", encoding="cp1251") as f:
    f.write(text)
