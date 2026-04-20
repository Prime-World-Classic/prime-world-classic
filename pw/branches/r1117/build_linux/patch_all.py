import re

def patch_splash():
    path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PF_GameLogic/TutorialSplash.cpp"
    with open(path, "r", encoding="cp1251") as f:
        text = f.read()

    # Add #ifdef _WIN32 before the big namespace
    text = re.sub(r'(namespace\n\{\n#pragma pack)', r'#ifdef _WIN32\n\1', text)

    # Add #endif after NI_DEFINE_REFCOUNT(SplashJob);
    text = re.sub(r'(NI_DEFINE_REFCOUNT\(SplashJob\);\n)', r'\1\n#endif\n', text)

    # Replace the TutorialSplash implementation with #ifdef
    text = re.sub(r'(  TutorialSplash::TutorialSplash\(\)\n    : thread\(CreateSplashJobThread\(\)\))', r'#ifdef _WIN32\n\1', text)

    # Add #else and #endif for the TutorialSplash implementation
    text = re.sub(r'(      thread->Wait\(\);\n    \}\n  \}\n\})', r'\1\n#else\n  TutorialSplash::TutorialSplash() : thread(NULL) {}\n  TutorialSplash::~TutorialSplash() {}\n#endif\n', text)

    with open(path, "w", encoding="cp1251") as f:
        f.write(text)

def patch_weblauncher():
    path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PF_GameLogic/WebLauncher.cpp"
    with open(path, "r", encoding="cp1251") as f:
        text = f.read()

    # We'll just wrap everything after includes
    text = re.sub(r'(#include "WebLauncher.h"\n)', r'\1\n#ifdef _WIN32\n', text)
    text = text + "\n#else\nnamespace NGameX { void WebLauncherPostRequest(const char*, const std::vector<int>&, int) {} void WebLauncherPostRequest(const char*, const char*, int) {} void StartWebLauncher() {} }\n#endif\n"

    with open(path, "w", encoding="cp1251") as f:
        f.write(text)

def patch_worldchecker():
    path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PF_GameLogic/WorldChecker.cpp"
    with open(path, "r", encoding="cp1251") as f:
        text = f.read()
    
    text = re.sub(r'fopen_s\(&([^,]+),([^,]+),([^)]+)\)', r'(\1 = fopen(\2, \3)) == NULL', text)

    with open(path, "w", encoding="cp1251") as f:
        f.write(text)

patch_splash()
patch_weblauncher()
patch_worldchecker()

