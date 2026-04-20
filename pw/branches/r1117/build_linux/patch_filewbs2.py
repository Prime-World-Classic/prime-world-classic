import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/FileSystem/FileWriteBufferedStream.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

# Replace the wrong stubs
text = text.replace('bool FileWriteBufferedStream::IsOpened() const { return false; }\n', '')
text = text.replace('void FileWriteBufferedStream::Append(const void *pData, const int length) {}\n', '')
text = text.replace('void FileWriteBufferedStream::SetFilePosition(const int _distanceToMove, const int _distanceToMoveHigh, const int _moveMethod) {}', 'void FileWriteBufferedStream::SetFilePosition(long posLow, long posHigh, unsigned long moveFrom) {}')

with open(path, "w", encoding="cp1251") as f:
    f.write(text)

