import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/RegistryStorage.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

# Grab everything after includes
match = re.search(r'(#include "RegistryStorage\.h"\s*\n)([\s\S]*)', text)
if match:
    stub = """#ifdef _WIN32
""" + match.group(2) + """
#else

namespace registry {
  Storage::Storage(const nstl::string& company, const nstl::string& app) {}
  Storage::~Storage() {}

  void Storage::Write(const nstl::string& key, int val) {}
  void Storage::Write(const nstl::string& key, const nstl::string& val) {}
  void Storage::Write(const nstl::string& key, const void* val, int len) {}

  int Storage::Read(const nstl::string& key, int def) { return def; }
  nstl::string Storage::Read(const nstl::string& key, const nstl::string& def) { return def; }
  void Storage::Read(const nstl::string& key, void* val, int len) {}

  void Storage::OpenKey(HKEY root, const nstl::string& path, PHKEY key) {}
  void Storage::CloseKey(PHKEY key) {}
}

#endif
"""
    with open(path, "w", encoding="cp1251") as f:
        f.write(match.group(1) + stub)

