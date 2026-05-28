import sys

content = open("Src/Render/GLRenderer.cpp", "r").read()

if "glGetError()" not in content:
    content = content.replace(
        'STDMETHODIMP GLDirect3DDevice9::Present(CONST RECT* pSrc, CONST RECT* pDst, HWND hWnd, CONST RGNDATA* pReg) {',
        'STDMETHODIMP GLDirect3DDevice9::Present(CONST RECT* pSrc, CONST RECT* pDst, HWND hWnd, CONST RGNDATA* pReg) {\n    GLenum err; while ((err = glGetError()) != GL_NO_ERROR) { printf("GL Error before Present: %x\\n", err); }\n'
    )
    content = content.replace(
        'STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT bv, UINT min, UINT num, UINT si, UINT pc) {',
        'STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT bv, UINT min, UINT num, UINT si, UINT pc) {\n    GLenum err; while ((err = glGetError()) != GL_NO_ERROR) { printf("GL Error before Draw: %x\\n", err); }\n'
    )
    open("Src/Render/GLRenderer.cpp", "w").write(content)
