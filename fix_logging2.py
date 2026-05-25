import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("STDMETHODIMP GLDirect3DDevice9::SetRenderTarget(DWORD i, IDirect3DSurface9* pRt) {", 
                          "STDMETHODIMP GLDirect3DDevice9::SetRenderTarget(DWORD i, IDirect3DSurface9* pRt) { printf(\"SetRenderTarget %u, %p\\n\", i, pRt); fflush(stdout); ")

content = content.replace("STDMETHODIMP GLDirect3DDevice9::Clear(DWORD c, CONST D3DRECT* pR, DWORD f, D3DCOLOR col, float z, DWORD s) {",
                          "STDMETHODIMP GLDirect3DDevice9::Clear(DWORD c, CONST D3DRECT* pR, DWORD f, D3DCOLOR col, float z, DWORD s) { if(g_currentFBO==0) { printf(\"Clear FBO 0\\n\"); fflush(stdout); } ")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
