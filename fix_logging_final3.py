import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("void GLDirect3DDevice9::SetSDLWindow(void* w) { g_sdlWindow = w; }",
                          "void GLDirect3DDevice9::SetSDLWindow(void* w) { printf(\"SetSDLWindow %p\\n\", w); fflush(stdout); g_sdlWindow = w; }")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
