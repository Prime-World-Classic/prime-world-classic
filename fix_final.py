import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("void* g_sdlWindow = NULL;", "extern void* g_sdlWindow;")
content += "\nvoid GLDirect3DDevice9::SetSDLWindow(void* w) { g_sdlWindow = w; }\n"

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
