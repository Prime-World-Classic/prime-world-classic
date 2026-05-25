import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

# I want to extract the segment from "GLuint GLDirect3DDevice9::CompileShader" to "STDMETHODIMP GLDirect3DDevice9::ProcessVertices"

start_idx = content.find("STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(")
end_idx = content.find("STDMETHODIMP GLDirect3DDevice9::ProcessVertices(")

if start_idx != -1 and end_idx != -1:
    new_code = """STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) {
    g_drawCalls++; GLenum mode = GetGLPrimitiveType(PrimitiveType); GLsizei count = GetGLVertexCount(PrimitiveType, primCount);
    UpdateShaderProgram();
    ApplyAttributes(NULL, 0, BaseVertexIndex);
    GLenum indexType = GL_UNSIGNED_SHORT; int indexSize = 2;
    if (m_pIndexData) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ((GLDirect3DIndexBuffer9*)m_pIndexData)->GetIBO()); if (((GLDirect3DIndexBuffer9*)m_pIndexData)->GetFormat() == D3DFMT_INDEX32) { indexType = GL_UNSIGNED_INT; indexSize = 4; } }
    glDrawElements(mode, count, indexType, (void*)(uintptr_t)(startIndex * indexSize));
    glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) {
    g_drawCalls++; UpdateShaderProgram(); GLenum mode = GetGLPrimitiveType(PrimitiveType); GLsizei count = GetGLVertexCount(PrimitiveType, PrimitiveCount);
    ApplyAttributes(NULL, 0, StartVertex);
    glDrawArrays(mode, 0, count);
    glBindBuffer(GL_ARRAY_BUFFER, 0); return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) {
    g_drawCalls++; UpdateShaderProgram(); GLenum mode = GetGLPrimitiveType(PrimitiveType); GLsizei count = GetGLVertexCount(PrimitiveType, PrimitiveCount);
    ApplyAttributes(pVertexStreamZeroData, VertexStreamZeroStride, 0);
    glDrawArrays(mode, 0, count); return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) {
    g_drawCalls++; UpdateShaderProgram(); GLenum mode = GetGLPrimitiveType(PrimitiveType); GLsizei count = GetGLVertexCount(PrimitiveType, PrimitiveCount);
    GLenum indexType = (IndexDataFormat == D3DFMT_INDEX32) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    ApplyAttributes(pVertexStreamZeroData, VertexStreamZeroStride, 0);
    glDrawElements(mode, count, indexType, pIndexData); return D3D_OK; }

"""
    new_content = content[:start_idx] + new_code + content[end_idx:]
    open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(new_content)
    print("Patched GLRenderer.cpp successfully.")
else:
    print("Could not find blocks.")
