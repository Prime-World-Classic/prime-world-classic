import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("STDMETHODIMP GLDirect3DDevice9::DrawPrimitive(D3DPRIMITIVETYPE t, UINT sv, UINT pc) { g_drawCalls++; UpdateShaderProgram(); ApplyAttributes(NULL, 0, sv); glDrawArrays(GetGLPrim(t), 0, GetGLCount(t,pc)); return D3D_OK; }",
                          "STDMETHODIMP GLDirect3DDevice9::DrawPrimitive(D3DPRIMITIVETYPE t, UINT sv, UINT pc) { g_drawCalls++; UpdateShaderProgram(); ApplyAttributes(NULL, 0, sv); if(g_drawCalls < 200) printf(\"DP FBO=%u, pc=%d\\n\", g_currentFBO, pc); glDrawArrays(GetGLPrim(t), 0, GetGLCount(t,pc)); return D3D_OK; }")

content = content.replace("STDMETHODIMP GLDirect3DDevice9::DrawPrimitiveUP(D3DPRIMITIVETYPE t, UINT pc, CONST void* d, UINT s) { g_drawCalls++; UpdateShaderProgram(); ApplyAttributes(d, s, 0); glDrawArrays(GetGLPrim(t), 0, GetGLCount(t,pc)); return D3D_OK; }",
                          "STDMETHODIMP GLDirect3DDevice9::DrawPrimitiveUP(D3DPRIMITIVETYPE t, UINT pc, CONST void* d, UINT s) { g_drawCalls++; UpdateShaderProgram(); ApplyAttributes(d, s, 0); if(g_drawCalls < 200) printf(\"DPUP FBO=%u, pc=%d, s=%u\\n\", g_currentFBO, pc, s); glDrawArrays(GetGLPrim(t), 0, GetGLCount(t,pc)); return D3D_OK; }")

content = content.replace("STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t, UINT min, UINT num, UINT pc, CONST void* i, D3DFORMAT f, CONST void* d, UINT s) { g_drawCalls++; UpdateShaderProgram(); ApplyAttributes(d, s, 0); glDrawElements(GetGLPrim(t), GetGLCount(t,pc), (f==D3DFMT_INDEX32?GL_UNSIGNED_INT:GL_UNSIGNED_SHORT), i); return D3D_OK; }",
                          "STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t, UINT min, UINT num, UINT pc, CONST void* i, D3DFORMAT f, CONST void* d, UINT s) { g_drawCalls++; UpdateShaderProgram(); ApplyAttributes(d, s, 0); if(g_drawCalls < 200) printf(\"DIPUP FBO=%u, pc=%d, s=%u\\n\", g_currentFBO, pc, s); glDrawElements(GetGLPrim(t), GetGLCount(t,pc), (f==D3DFMT_INDEX32?GL_UNSIGNED_INT:GL_UNSIGNED_SHORT), i); return D3D_OK; }")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
