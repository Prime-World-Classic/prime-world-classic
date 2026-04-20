import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PF_GameLogic/TutorialSplash.cpp"
with open(path, "r") as f:
    text = f.read()

# Add #ifdef _WIN32 before the big namespace
text = re.sub(r'namespace\n\{\n#pragma pack', '#ifdef _WIN32\nnamespace\n{\n#pragma pack', text)

# Add #endif after NI_DEFINE_REFCOUNT(SplashJob);
text = re.sub(r'NI_DEFINE_REFCOUNT\(SplashJob\);\n\nnamespace NGameX', 'NI_DEFINE_REFCOUNT(SplashJob);\n\n#endif\n\nnamespace NGameX', text)

# Replace the TutorialSplash implementation with #ifdef
text = re.sub(r'  TutorialSplash::TutorialSplash\(\)\n    : thread\(CreateSplashJobThread\(\)\)', '#ifdef _WIN32\n  TutorialSplash::TutorialSplash()\n    : thread(CreateSplashJobThread())', text)

# Add #else and #endif for the TutorialSplash implementation
text = re.sub(r'      thread->Wait\(\);\n    \}\n  \}\n\}', '      thread->Wait();\n    }\n  }\n#else\n  TutorialSplash::TutorialSplash() : thread(NULL) {}\n  TutorialSplash::~TutorialSplash() {}\n#endif\n}', text)

with open(path, "w") as f:
    f.write(text)
