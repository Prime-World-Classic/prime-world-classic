import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

target = "STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT bv, UINT min, UINT num, UINT si, UINT pc) {"
log_code = """STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT bv, UINT min, UINT num, UINT si, UINT pc) {
    g_drawCalls++; UpdateShaderProgram(); ApplyAttributes(NULL, 0, bv);
    if (g_drawCalls < 100) {
        if (m_streams[0].pStreamData) {
            float* f = (float*)((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->m_pData;
            if (f) printf("DrawIndexedPrimitive FBO=%u, pc=%d, stride=%u, v=(%.2f, %.2f, %.2f)\\n", g_currentFBO, pc, m_streams[0].Stride, f[0], f[1], f[2]);
        }
        fflush(stdout);
    }
"""

if target in content:
    content = content.replace(target, log_code)
    open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
    print("Logging added.")
else:
    print("Target not found.")
