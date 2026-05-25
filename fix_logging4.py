import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("STDMETHODIMP GLDirect3DDevice9::CreateTexture(UINT w, UINT h, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DTexture9** ppT, HANDLE* ph) {",
                          "STDMETHODIMP GLDirect3DDevice9::CreateTexture(UINT w, UINT h, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DTexture9** ppT, HANDLE* ph) { if(u & D3DUSAGE_RENDERTARGET) printf(\"CreateTexture RT %ux%u\\n\", w, h); ")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
