import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("glGenFramebuffers(1, &m_fbo);",
                          "glGenFramebuffers(1, &m_fbo); printf(\"FBO %u created for tex %ux%u\\n\", m_fbo, m_width, m_height); fflush(stdout);")

content = content.replace("STDMETHODIMP GLDirect3DDevice9::StretchRect(IDirect3DSurface9* pS, CONST RECT* pSR, IDirect3DSurface9* pD, CONST RECT* pDR, D3DTEXTUREFILTERTYPE f) {",
                          "STDMETHODIMP GLDirect3DDevice9::StretchRect(IDirect3DSurface9* pS, CONST RECT* pSR, IDirect3DSurface9* pD, CONST RECT* pDR, D3DTEXTUREFILTERTYPE f) { printf(\"StretchRect!\\n\"); fflush(stdout); ")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
