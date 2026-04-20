import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/ExecutionMemoryManager.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

# Replace the previous stub
match = re.search(r'(#include "ExecutionMemoryManager\.h"\s*\n)([\s\S]*)', text)
if match:
    # First, strip off the old stub if it exists
    content = match.group(2)
    content = re.sub(r'#else\s*ExecutionMemoryManager::ExecutionMemoryManager.*#endif', '', content, flags=re.DOTALL)
    
    new_content = match.group(1) + content + """
#ifndef _WIN32

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


fp_path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/FileSystem/FilePath.cpp"
with open(fp_path, "r", encoding="cp1251") as f:
    fp_text = f.read()

fp_text = re.sub(r'(void SetModuleCurrentDir\(\)\n\{[\s\S]*?\n\})', 
r"""#ifdef _WIN32
\1
#else
#include <unistd.h>
void SetModuleCurrentDir()
{
    char szFileName[4096];
    ssize_t count = readlink("/proc/self/exe", szFileName, 4096);
    if (count != -1) {
        szFileName[count] = '\0';
        std::string path(szFileName);
        std::string::size_type n = path.rfind(NFile::FILE_SEPARATOR);
        if (n != std::string::npos) {
            std::string szDirName = path.substr(0, n);
            chdir(szDirName.c_str());
        }
    }
}
#endif
""", fp_text)

with open(fp_path, "w", encoding="cp1251") as f:
    f.write(fp_text)

