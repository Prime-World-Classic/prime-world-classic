import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/AppInstancesLimit.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

text = re.sub(r'(bool AppInstancesLimit::Lock\( int limit \)\n\{[\s\S]*?\n\})', r'#ifdef _WIN32\n\1\n#else\nbool AppInstancesLimit::Lock( int limit ) { return true; }\n#endif', text)

text = re.sub(r'(AppInstancesLimit::~AppInstancesLimit\(\)\n\{[\s\S]*?\n\})', r'#ifdef _WIN32\n\1\n#else\nAppInstancesLimit::~AppInstancesLimit() {}\n#endif', text)

with open(path, "w", encoding="cp1251") as f:
    f.write(text)
