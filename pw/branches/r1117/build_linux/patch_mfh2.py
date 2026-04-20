import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/MainFrame.h"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

text = text.replace('#include "nstring.h"\n#include "nvector.h"\n#include <vector>\nusing std::vector;\nusing std::wstring;\n', '#include "nstring.h"\n#include "nvector.h"\n')
text = text.replace('const vector<wstring>', 'const nstl::vector<nstl::wstring>')

with open(path, "w", encoding="cp1251") as f:
    f.write(text)
