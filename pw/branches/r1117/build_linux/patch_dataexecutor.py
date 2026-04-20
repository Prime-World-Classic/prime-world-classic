import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/DataExecutor.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

# Wrap everything after includes in #ifdef _WIN32
text = re.sub(
    r'(ExecutionMemoryManager\* DataExecutor::pExMemMgr = NULL;)',
    r'#ifdef _WIN32\n\1', text
)

stub = """
#else

ExecutionMemoryManager* DataExecutor::pExMemMgr = NULL;

struct DataExecutor::Impl {};

DataExecutor::DataExecutor() {}
DataExecutor::~DataExecutor() {}
int DataExecutor::SetConstant( const char * pString, float val ) { return 0; }
int DataExecutor::GetConstNumber( const char* pString ) { return 0; }
bool DataExecutor::Execute( const char* pString, float* pResult ) { return false; }
void DataExecutor::ExecuteFreeStackless( char* pFnc, char* pData ) {}
char* DataExecutor::GetBuffer() { return NULL; }
int DataExecutor::GetSize() const { return 0; }
void DataExecutor::ExecuteInternal(float* pResult) {}
void DataExecutor::CopyFunction( char* pBuffer, int nSize ) {}
void DataExecutor::CallStackless( char* pFnc, char* pData ) {}
int DataExecutor::GetLastExecutionStatus() const { return 0; }

#endif
"""

text = text + stub

with open(path, "w", encoding="cp1251") as f:
    f.write(text)
