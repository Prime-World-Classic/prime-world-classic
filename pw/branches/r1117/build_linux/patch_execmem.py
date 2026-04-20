import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/ExecutionMemoryManager.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

# Wrap everything after includes in #ifdef _WIN32
match = re.search(r'(#include "ExecutionMemoryManager\.h"\s*\n)([\s\S]*)', text)
if match:
    new_content = match.group(1) + "#ifdef _WIN32\n" + match.group(2) + """
#else

ExecutionMemoryManager::ExecutionMemoryManager() : pAllocated(NULL), nMaxSize(0), nUsedSize(0) {}
ExecutionMemoryManager::~ExecutionMemoryManager() {}
void* ExecutionMemoryManager::GetMemory(unsigned int size) { return NULL; }
void ExecutionMemoryManager::FreeMemory(void *pMem, unsigned int size) {}

#endif
"""
    with open(path, "w", encoding="cp1251") as f:
        f.write(new_content)
