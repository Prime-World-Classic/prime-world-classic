import re
import os

path = "pw/branches/r1117/Src/PW_Client/LobbyConnection.cpp"
os.system("git checkout " + path)

with open(path, "r", encoding="cp1251") as f:
    lines = f.readlines()

# We want to keep the file structure but provide Linux stubs
new_lines = []
for line in lines:
    if line.strip() == '#ifdef _WIN32':
        new_lines.append('#if 1\n')
    else:
        new_lines.append(line)

# Add #endif at the end if needed, or just let it be
# The original file has several #ifdef _WIN32

# Actually, the easiest way is to just wrap the problematic windows-only includes and code
content = "".join(new_lines)
content = content.replace('#include <winsock2.h>', '#ifdef _WIN32\n#include <winsock2.h>\n#endif')
content = content.replace('#include <Tlhelp32.h>', '#ifdef _WIN32\n#include <Tlhelp32.h>\n#endif')

# Replace windows socket code with stubs or ifdefs
# But the original file already has a #else block for non-win32!
# Let's just use that.

with open(path, "w", encoding="cp1251") as f:
    f.write(content)
