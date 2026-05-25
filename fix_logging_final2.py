import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

old_dip = """STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT bv, UINT min, UINT num, UINT si, UINT pc) {
    g_drawCalls++; UpdateShaderProgram(); ApplyAttributes(NULL, 0, bv);"""

new_dip = """STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT bv, UINT min, UINT num, UINT si, UINT pc) {
    g_drawCalls++; UpdateShaderProgram(); ApplyAttributes(NULL, 0, bv);
    if(g_drawCalls < 100) {
        float* f = NULL;
        if(m_streams[0].pStreamData) f = (float*)((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->m_pData;
        if(f) printf("DIP FBO=%u, pc=%d, v=(%.2f, %.2f, %.2f)\\n", g_currentFBO, pc, f[0], f[1], f[2]);
    }"""

if old_dip in content:
    content = content.replace(old_dip, new_dip)
    open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
    print("DIP logging added.")
else:
    print("DIP signature not found.")
