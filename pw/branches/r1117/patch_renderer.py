import sys

content = open("Src/Render/GLRenderer.cpp", "r").read()

content = content.replace(
    'STDMETHODIMP GLDirect3DDevice9::Clear(DWORD c, CONST D3DRECT* r_ptr, DWORD f, D3DCOLOR col, float z, DWORD s_val) {',
    'STDMETHODIMP GLDirect3DDevice9::Clear(DWORD c, CONST D3DRECT* r_ptr, DWORD f, D3DCOLOR col, float z, DWORD s_val) {\n    static int clearCount=0; if (clearCount++ % 60 == 0) printf("Clear FBO=%d color=%x\\n", g_currentFBO, col);'
)

content = content.replace(
    'STDMETHODIMP GLDirect3DDevice9::DrawPrimitive(D3DPRIMITIVETYPE t, UINT sv, UINT pc) {',
    'STDMETHODIMP GLDirect3DDevice9::DrawPrimitive(D3DPRIMITIVETYPE t, UINT sv, UINT pc) {\n    static int dpCount=0; if (dpCount++ % 60 == 0) printf("DrawPrimitive FBO=%d prims=%d tex=%p\\n", g_currentFBO, pc, m_textures[0]);'
)

content = content.replace(
    'STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT bv, UINT min, UINT num, UINT si, UINT pc) {',
    'STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT bv, UINT min, UINT num, UINT si, UINT pc) {\n    static int dipCount=0; if (dipCount++ % 60 == 0) printf("DrawIndexedPrimitive FBO=%d prims=%d tex=%p\\n", g_currentFBO, pc, m_textures[0]);'
)

open("Src/Render/GLRenderer.cpp", "w").write(content)
