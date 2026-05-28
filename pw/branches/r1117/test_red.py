import sys

content = open("Src/Render/GLRenderer.cpp", "r").read()

content = content.replace(
    'STDMETHODIMP GLDirect3DDevice9::Present(CONST RECT* pSrc, CONST RECT* pDst, HWND hWnd, CONST RGNDATA* pReg) {\n    if (g_sdlWindow) {\n        if (glIsFramebuffer(1)) {\n            glBindFramebuffer(GL_READ_FRAMEBUFFER, 1);\n            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);\n            glBlitFramebuffer(0, 0, 1024, 768, 0, 0, 1024, 768, GL_COLOR_BUFFER_BIT, GL_NEAREST);\n            glBindFramebuffer(GL_FRAMEBUFFER, g_currentFBO);\n        }',
    'STDMETHODIMP GLDirect3DDevice9::Present(CONST RECT* pSrc, CONST RECT* pDst, HWND hWnd, CONST RGNDATA* pReg) {\n    if (g_sdlWindow) {\n        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);\n        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);\n        glClear(GL_COLOR_BUFFER_BIT);\n        if (glIsFramebuffer(1)) {\n            glBindFramebuffer(GL_READ_FRAMEBUFFER, 1);\n            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);\n            glBlitFramebuffer(0, 0, 1024, 768, 0, 0, 1024, 768, GL_COLOR_BUFFER_BIT, GL_NEAREST);\n            glBindFramebuffer(GL_FRAMEBUFFER, g_currentFBO);\n        }'
)

open("Src/Render/GLRenderer.cpp", "w").write(content)
