import re

path_cm = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/CMakeLists.txt"
with open(path_cm, "r", encoding="cp1251") as f:
    text_cm = f.read()

text_cm = re.sub(r'(list_remove_regexp\( ALL_SRCS "\.\+/StackWalker\\\\\.cpp\$" \))', r'\1\nlist_remove_regexp( ALL_SRCS ".+/PerfMonitor\\\\.cpp$" )', text_cm)

with open(path_cm, "w", encoding="cp1251") as f:
    f.write(text_cm)


path_events = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Node/EventsQueue.cpp"
with open(path_events, "r", encoding="cp1251") as f:
    text_events = f.read()

text_events = re.sub(r'GetTickCount\(\)', r'0', text_events)

# The compilation error was about CEventNode being incomplete. Let's make sure Entity.h is included.
if '#include "Entity.h"' not in text_events:
    text_events = text_events.replace('#include "EventsQueue.h"', '#include "Entity.h"\n#include "EventsQueue.h"')

with open(path_events, "w", encoding="cp1251") as f:
    f.write(text_events)


path_tm = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Node/ThreadManager.cpp"
with open(path_tm, "r", encoding="cp1251") as f:
    text_tm = f.read()

text_tm = re.sub(r'GetTickCount\(\)', r'0', text_tm)

with open(path_tm, "w", encoding="cp1251") as f:
    f.write(text_tm)

