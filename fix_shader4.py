import sys

with open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "r") as f:
    content = f.read()

target = """STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) {
    g_drawCalls++; GLenum mode = GetGLPrimitiveType(PrimitiveType); GLsizei count = GetGLVertexCount(PrimitiveType, primCount);
    
    bool hasPositionT = false;
    if (m_pVertexDecl) {
        const std::vector<D3DVERTEXELEMENT9>& elements = ((GLDirect3DVertexDeclaration9*)m_pVertexDecl)->GetElements();
        for (size_t i = 0; i < elements.size(); ++i) { if (elements[i].Usage == D3DDECLUSAGE_POSITIONT) hasPositionT = true; }
    }
    UINT stride = m_streams[0].Stride;
    bool isRHW = hasPositionT || (m_fvf & D3DFVF_XYZRHW) != 0 || stride == 20;

    if (g_drawCalls < 200) { 
        printf("DrawIndexedPrimitive FBO=%u, count=%d, stride=%u, isRHW=%d\\n", g_currentFBO, count, stride, isRHW); 
        if (m_streams[0].pStreamData) {
            float* f = (float*)((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->m_pData;
            if (f) printf("  first_v=(%.2f, %.2f, %.2f)\\n", f[0], f[1], f[2]);
        }"""

new_target = """STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) {
    g_drawCalls++; GLenum mode = GetGLPrimitiveType(PrimitiveType); GLsizei count = GetGLVertexCount(PrimitiveType, primCount);
    
    bool hasPositionT = false;
    if (m_pVertexDecl) {
        const std::vector<D3DVERTEXELEMENT9>& elements = ((GLDirect3DVertexDeclaration9*)m_pVertexDecl)->GetElements();
        for (size_t i = 0; i < elements.size(); ++i) { if (elements[i].Usage == D3DDECLUSAGE_POSITIONT) hasPositionT = true; }
    }
    UINT stride = m_streams[0].Stride;
    bool isRHW = hasPositionT || (m_fvf & D3DFVF_XYZRHW) != 0 || stride == 20;

    if (g_drawCalls < 200) { 
        if (m_streams[0].pStreamData) {
            float* f = (float*)((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->m_pData;
            if (f) printf("DrawIndexedPrimitive FBO=%u, count=%d, stride=%u, isRHW=%d, v=(%.2f, %.2f, %.2f)\\n", g_currentFBO, count, stride, isRHW, f[0], f[1], f[2]);
        }
        fflush(stdout); 
    }"""

if target in content:
    content = content.replace(target, new_target)
    print("Replaced DrawIndexedPrimitive log.")
    with open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w") as f:
        f.write(content)
else:
    print("Not found target.")
