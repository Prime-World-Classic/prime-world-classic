import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

# Add isRHW parameter to UpdateShaderProgram
content = content.replace("void GLDirect3DDevice9::UpdateShaderProgram() {",
                          "void GLDirect3DDevice9::UpdateShaderProgram(bool isRHW) {")

# Update call sites of UpdateShaderProgram
content = content.replace("UpdateShaderProgram();", "UpdateShaderProgram(isRHW_flag);")

# Add isRHW_flag logic to Draw calls
content = content.replace("STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT bv, UINT min, UINT num, UINT si, UINT pc) {\n    g_drawCalls++; UpdateShaderProgram(isRHW_flag); ApplyAttributes(NULL, 0, bv);",
                          "STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT bv, UINT min, UINT num, UINT si, UINT pc) {\n    g_drawCalls++;\n    float* f = NULL; if(m_streams[0].pStreamData) f = (float*)((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->m_pData;\n    bool isRHW_flag = (f && (abs(f[0]) > 2.0f || abs(f[1]) > 2.0f));\n    UpdateShaderProgram(isRHW_flag); ApplyAttributes(NULL, 0, bv);")

content = content.replace("STDMETHODIMP GLDirect3DDevice9::DrawPrimitive(D3DPRIMITIVETYPE t, UINT sv, UINT pc) { g_drawCalls++; UpdateShaderProgram(isRHW_flag); ApplyAttributes(NULL, 0, sv);",
                          "STDMETHODIMP GLDirect3DDevice9::DrawPrimitive(D3DPRIMITIVETYPE t, UINT sv, UINT pc) {\n    g_drawCalls++;\n    float* f = NULL; if(m_streams[0].pStreamData) f = (float*)((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->m_pData;\n    bool isRHW_flag = (f && (abs(f[0]) > 2.0f || abs(f[1]) > 2.0f));\n    UpdateShaderProgram(isRHW_flag); ApplyAttributes(NULL, 0, sv);")

content = content.replace("STDMETHODIMP GLDirect3DDevice9::DrawPrimitiveUP(D3DPRIMITIVETYPE t, UINT pc, CONST void* d, UINT s) { g_drawCalls++; UpdateShaderProgram(isRHW_flag); ApplyAttributes(d, s, 0);",
                          "STDMETHODIMP GLDirect3DDevice9::DrawPrimitiveUP(D3DPRIMITIVETYPE t, UINT pc, CONST void* d, UINT s) {\n    g_drawCalls++;\n    float* f = (float*)d;\n    bool isRHW_flag = (f && (abs(f[0]) > 2.0f || abs(f[1]) > 2.0f));\n    UpdateShaderProgram(isRHW_flag); ApplyAttributes(d, s, 0);")

# Remove old isRHW detection from UpdateShaderProgram
import re
content = re.sub(r'float\* f_ptr = NULL;.*?bool isRHW = .*?;', '', content, flags=re.DOTALL)

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
