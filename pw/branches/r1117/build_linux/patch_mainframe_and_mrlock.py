import re

path_cm = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/CMakeLists.txt"
with open(path_cm, "r", encoding="cp1251") as f:
    text_cm = f.read()
text_cm = re.sub(r'list_remove_regexp\( ALL_SRCS "\.\+/FlashWindow\\\\\.cpp\$" \)', r'list_remove_regexp( ALL_SRCS ".+/FlashWindow\\\\.cpp$" )\nlist_remove_regexp( ALL_SRCS ".+/MultiReaderLock\\\\.cpp$" )', text_cm)
with open(path_cm, "w", encoding="cp1251") as f:
    f.write(text_cm)


path_mfh = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/MainFrame.h"
with open(path_mfh, "r", encoding="cp1251") as f:
    text_mfh = f.read()

# Make sure nstring and vector are known.
text_mfh = re.sub(r'#include "HPTimer\.h"', r'#include "HPTimer.h"\n#include "nstring.h"\n#include "nvector.h"\n#include <vector>\nusing std::vector;\nusing std::wstring;\n#ifndef _interface\n#define _interface struct\n#endif\n#include "IBaseInterface.h"', text_mfh)

with open(path_mfh, "w", encoding="cp1251") as f:
    f.write(text_mfh)

