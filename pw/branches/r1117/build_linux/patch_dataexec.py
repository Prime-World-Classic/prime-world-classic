import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/DataExecutor.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

# We need to grab everything after the includes and wrap it
match = re.search(r'(#include "DataExecutor\.h"\s*\n)([\s\S]*)', text)
if match:
    new_content = match.group(1) + "#ifdef _WIN32\n" + match.group(2) + """
#else
ScopedPtr<ExecutionMemoryManager> DataExecutor::memoryManager;
int DataExecutor::nLastExecutionStatus = 0;

void DataExecutor::fixDirectRelocs(unsigned int const nBase, unsigned int const nShift, unsigned int const *npOffsetTable, unsigned int nTableSize) {}
void DataExecutor::fixDirectRelocs(unsigned int const nBase, unsigned int const *npOffsetTable, unsigned int nTableSize) {}
void DataExecutor::initClass(unsigned char expectedVersion, unsigned char const *dataBuffer, unsigned int nBufferSize) {}
bool DataExecutor::CheckFPUStack() { return true; }
void __cdecl DataExecutor::ExecuteFreeStackless() const {}

DataExecutor::DataExecutor(unsigned char expectedVersion, unsigned char const *dataBuffer, unsigned int nBufferSize) : pBinaryCode(NULL), nEntryPointOffset(0) {}
DataExecutor::DataExecutor(unsigned char expectedVersion, char const *cpBase64String) : pBinaryCode(NULL), nEntryPointOffset(0) {}
DataExecutor::~DataExecutor() {}
int DataExecutor::GetLastExecutionStatus() { return 0; }
void DataExecutor::Execute(char const retType, char const *argsType, ...) const {}
void DataExecutor::ExecuteV(char const retType, unsigned int const stackSize, ...) const {}
#endif
"""
    with open(path, "w", encoding="cp1251") as f:
        f.write(new_content)
