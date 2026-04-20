import re

path_pe = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/PersistEvents.cpp"
with open(path_pe, "r", encoding="cp1251") as f:
    text_pe = f.read()
text_pe = text_pe.replace('#include "system/math/md4.h"', '#include "System/Math/MD4.h"')
with open(path_pe, "w", encoding="cp1251") as f:
    f.write(text_pe)

path_eq = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Node/EventsQueue.cpp"
with open(path_eq, "r", encoding="cp1251") as f:
    text_eq = f.read()

# Prepend stdafx.h
text_eq = '#include "stdafx.h"\n' + text_eq

with open(path_eq, "w", encoding="cp1251") as f:
    f.write(text_eq)
