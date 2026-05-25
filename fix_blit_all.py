import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

old_present = """STDMETHODIMP GLDirect3DDevice9::Present(CONST RECT* pSrc, CONST RECT* pDst, HWND hWnd, CONST RGNDATA* pReg) {
    if (g_sdlWindow) {
        static int frames = 0; if (++frames % 60 == 0) { printf("Present frame %d (draws: %d)\\n", frames, g_drawCalls); fflush(stdout); }
        for (GLuint fbo = 1; fbo <= 5; fbo++) {
            if (glIsFramebuffer(fbo)) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                NiBlitFramebuffer(0, 0, 1024, 768, 0, 768, 1024, 0, GL_COLOR_BUFFER_BIT, GL_LINEAR);
                glGetError();
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, 1024, 768); 
        SDL_GL_SwapWindow((SDL_Window*)g_sdlWindow);
    }
    return D3D_OK;
}"""

new_present = """STDMETHODIMP GLDirect3DDevice9::Present(CONST RECT* pSrc, CONST RECT* pDst, HWND hWnd, CONST RGNDATA* pReg) {
    if (g_sdlWindow) {
        static int frames = 0; if (++frames % 60 == 0) { printf("Present frame %d (draws: %d)\\n", frames, g_drawCalls); fflush(stdout); }
        
        // AGGRESSIVE BLIT: Try to blit everything that looks like a full-screen RT
        for (GLuint fbo = 1; fbo <= 16; fbo++) {
            if (glIsFramebuffer(fbo)) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                // Blit with both normal and flipped Y to be safe
                glBlitFramebuffer(0, 0, 1024, 768, 0, 0, 1024, 768, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                glGetError();
            }
        }
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        SDL_GL_SwapWindow((SDL_Window*)g_sdlWindow);
    }
    return D3D_OK;
}"""

if old_present in content:
    content = content.replace(old_present, new_present)
    open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
    print("Patched Present for all FBOs.")
else:
    print("Present signature not found.")
