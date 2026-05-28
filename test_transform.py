import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("STDMETHODIMP GLDirect3DDevice9::SetTransform(D3DTRANSFORMSTATETYPE s, CONST D3DMATRIX* m) { return D3D_OK; }",
                          "STDMETHODIMP GLDirect3DDevice9::SetTransform(D3DTRANSFORMSTATETYPE s, CONST D3DMATRIX* m) { if(m) { printf(\"SetTransform %d:\\n%f %f %f %f\\n%f %f %f %f\\n%f %f %f %f\\n%f %f %f %f\\n\", s, m->m[0][0], m->m[0][1], m->m[0][2], m->m[0][3], m->m[1][0], m->m[1][1], m->m[1][2], m->m[1][3], m->m[2][0], m->m[2][1], m->m[2][2], m->m[2][3], m->m[3][0], m->m[3][1], m->m[3][2], m->m[3][3]); fflush(stdout); } return D3D_OK; }")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
