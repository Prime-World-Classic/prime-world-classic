import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/ExecutionMemoryManager.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

match = re.search(r'(#include "ExecutionMemoryManager\.h"\s*\n)([\s\S]*)', text)
if match:
    new_content = match.group(1) + "#ifdef _WIN32\n" + match.group(2) + """
#else

ExecutionMemoryManager::ExecutionMemoryManager(unsigned int, unsigned int) {}
ExecutionMemoryManager::~ExecutionMemoryManager() {}
void* ExecutionMemoryManager::Alloc(unsigned int) { return NULL; }
void ExecutionMemoryManager::Free(void*) {}
ExecutionMemoryManager::LinkTableEntry const* ExecutionMemoryManager::GetEntryByName(char const*, int&) { return NULL; }
int ExecutionMemoryManager::GetSymbolOffset(char const*) { return 0; }
int ExecutionMemoryManager::GetLinkCount() { return 0; }

#endif
"""
    with open(path, "w", encoding="cp1251") as f:
        f.write(new_content)
