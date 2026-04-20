import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/AssertDumper.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

text = re.sub(r'(static int CatchException\( const struct tm &tim, EXCEPTION_POINTERS\* pExceptionInfo \)\n\{[\s\S]*?\}\n)', r'#ifdef _WIN32\n\1', text)
text = re.sub(r'(static void GenerateException\(const struct tm & tim\)\n\{[\s\S]*?\}\n\})', r'\1\n#else\nstatic void GenerateException(const struct tm &) {}\n#endif\n', text)

with open(path, "w", encoding="cp1251") as f:
    f.write(text)
