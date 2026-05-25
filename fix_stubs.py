import sys

missing_funcs = """
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
    glDrawElements(mode, count, indexType, pIndexData); return D3D_OK; 
}
STDMETHODIMP GLDirect3DDevice9::ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDeclaration, DWORD Flags) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateVertexDeclaration(CONST D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl) { 
    if (ppDecl) { *ppDecl = new GLDirect3DVertexDeclaration9(pVertexElements); } 
    return D3D_OK; 
}
STDMETHODIMP GLDirect3DDevice9::SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl) { m_pVertexDecl = pDecl; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetVertexDeclaration(IDirect3DVertexDeclaration9** ppDecl) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetFVF(DWORD FVF) { m_fvf = FVF; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetFVF(DWORD* pFVF) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::CreateVertexShader(CONST DWORD* pFunction, IDirect3DVertexShader9** ppShader) { if (ppShader) { *ppShader = new GLDirect3DVertexShader9(pFunction); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::SetVertexShader(IDirect3DVertexShader9* pShader) { m_pVertexShader = pShader; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetVertexShader(IDirect3DVertexShader9** ppShader) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetVertexShaderConstantF(UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount) { 
    if (StartRegister + Vector4fCount <= 256) { memcpy(&m_vsConstF[StartRegister], pConstantData, Vector4fCount * 16); } return D3D_OK; 
}
STDMETHODIMP GLDirect3DDevice9::GetVertexShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetVertexShaderConstantI(UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetVertexShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetVertexShaderConstantB(UINT StartRegister, CONST BOOL* pConstantData, UINT  BoolCount) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetVertexShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::CreatePixelShader(CONST DWORD* pFunction, IDirect3DPixelShader9** ppShader) { if (ppShader) { *ppShader = new GLDirect3DPixelShader9(pFunction); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::SetPixelShader(IDirect3DPixelShader9* pShader) { m_pPixelShader = pShader; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetPixelShader(IDirect3DPixelShader9** ppShader) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetPixelShaderConstantF(UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount) { if (StartRegister + Vector4fCount <= 256) { memcpy(&m_psConstF[StartRegister], pConstantData, Vector4fCount * 16); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetPixelShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetPixelShaderConstantI(UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetPixelShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetPixelShaderConstantB(UINT StartRegister, CONST BOOL* pConstantData, UINT  BoolCount) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetPixelShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::DrawRectPatch(UINT Handle, CONST float* pNumSegs, CONST D3DRECTPATCH_INFO* pPatchInfo) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::DrawTriPatch(UINT Handle, CONST float* pNumSegs, CONST D3DTRIPATCH_INFO* pPatchInfo) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::DeletePatch(UINT Handle) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery) { if (ppQuery) { *ppQuery = new GLDirect3DQuery9(); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) { 
    if (StreamNumber < 16) { m_streams[StreamNumber].pStreamData = pStreamData; m_streams[StreamNumber].OffsetInBytes = OffsetInBytes; m_streams[StreamNumber].Stride = Stride; } 
    return D3D_OK; 
}
STDMETHODIMP GLDirect3DDevice9::GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* OffsetInBytes, UINT* pStride) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetStreamSourceFreq(UINT StreamNumber, UINT Setting) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetStreamSourceFreq(UINT StreamNumber, UINT* pSetting) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetIndices(IDirect3DIndexBuffer9* pIndexData) { m_pIndexData = pIndexData; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetIndices(IDirect3DIndexBuffer9** ppIndexData) { return E_NOTIMPL; }
"""

with open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "a") as f:
    f.write(missing_funcs)
