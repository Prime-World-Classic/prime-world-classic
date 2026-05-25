import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("STDMETHODIMP GLDirect3DDevice9::CreateRenderTarget(UINT w, UINT h, D3DFORMAT f, D3DMULTISAMPLE_TYPE ms, DWORD q, BOOL l, IDirect3DSurface9** ppS, HANDLE* ph) {",
                          "STDMETHODIMP GLDirect3DDevice9::CreateRenderTarget(UINT w, UINT h, D3DFORMAT f, D3DMULTISAMPLE_TYPE ms, DWORD q, BOOL l, IDirect3DSurface9** ppS, HANDLE* ph) { printf(\"CreateRenderTarget %ux%u, fmt=%d\\n\", w, h, f); fflush(stdout); ")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
