import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/FileSystem/FileWriteBufferedStream.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

# Wrap all methods in #ifdef _WIN32
match = re.search(r'(FileWriteBufferedStream::FileWriteBufferedStream\(\)[\s\S]*)', text)
if match:
    content = match.group(1)
    
    new_content = """#ifdef _WIN32
""" + content + """
#else

FileWriteBufferedStream::FileWriteBufferedStream() : file(-1), buffer(NULL), size(0), position(0), creationMode(0) {}
FileWriteBufferedStream::FileWriteBufferedStream(const string & _fileName) : file(-1), buffer(NULL), size(0), position(0), creationMode(0) {}
FileWriteBufferedStream::FileWriteBufferedStream(const string & _fileName, const int _bufferSize, const int _fileSize) : file(-1), buffer(NULL), size(0), position(0), creationMode(0) {}
FileWriteBufferedStream::~FileWriteBufferedStream() {}

void FileWriteBufferedStream::InitInternal() {}
void FileWriteBufferedStream::OpenInternal(const string & _fileName) {}
void FileWriteBufferedStream::SetSizeInternal( const int _size ) {}
char * FileWriteBufferedStream::AllocateBuffer( const int _size ) { return NULL; }
void FileWriteBufferedStream::FreeBuffer( char ** buffer ) {}
int FileWriteBufferedStream::ReadInternal( void *pData, const int length ) { return 0; }
int FileWriteBufferedStream::WriteInternal( const void *pData, const int length ) { return 0; }
void FileWriteBufferedStream::Open(const string & _fileName) {}
void FileWriteBufferedStream::Close() {}
bool FileWriteBufferedStream::IsOpened() const { return false; }
void FileWriteBufferedStream::SetFileSize(const int _fileSize) {}
void FileWriteBufferedStream::SetFilePosition(const int _distanceToMove, const int _distanceToMoveHigh, const int _moveMethod) {}
void FileWriteBufferedStream::SetEndOfFile() {}
void FileWriteBufferedStream::ReserveBuffer( const int _bufferSize ) {}
void FileWriteBufferedStream::Flush() {}
void FileWriteBufferedStream::Append(const void *pData, const int length) {}

#endif
"""
    with open(path, "w", encoding="cp1251") as f:
        f.write(text[:match.start()] + new_content)

