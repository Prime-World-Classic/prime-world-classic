path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/RTTI.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

text = text.replace('strcpy_s(Get(mangledSearchName), nSize, sMangled.c_str());', 'strncpy(Get(mangledSearchName), sMangled.c_str(), nSize);')

with open(path, "w", encoding="cp1251") as f:
    f.write(text)
