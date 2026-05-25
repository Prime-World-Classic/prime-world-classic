import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("STDMETHODIMP GLDirect3DDevice9::BeginScene() { g_drawCalls = 0; return D3D_OK; }",
                          "STDMETHODIMP GLDirect3DDevice9::BeginScene() { g_drawCalls = 0; glBindFramebuffer(GL_FRAMEBUFFER, 1); glClearColor(1,0,1,1); glClear(GL_COLOR_BUFFER_BIT); glBindFramebuffer(GL_FRAMEBUFFER, 0); return D3D_OK; }")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
