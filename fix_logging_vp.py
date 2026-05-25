import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("STDMETHODIMP GLDirect3DDevice9::SetViewport(CONST D3DVIEWPORT9* v) { if(v) glViewport(v->X, 768-(v->Y+v->Height), v->Width, v->Height); return D3D_OK; }",
                          "STDMETHODIMP GLDirect3DDevice9::SetViewport(CONST D3DVIEWPORT9* v) { if(v) { printf(\"SetViewport %u,%u %ux%u\\n\", v->X, v->Y, v->Width, v->Height); fflush(stdout); glViewport(v->X, 768-(v->Y+v->Height), v->Width, v->Height); } return D3D_OK; }")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
