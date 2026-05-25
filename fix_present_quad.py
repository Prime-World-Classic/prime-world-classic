import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

start_sig = "STDMETHODIMP GLDirect3DDevice9::Present("
end_sig = "return D3D_OK;\n}"

start_idx = content.find(start_sig)
end_idx = content.find(end_sig, start_idx) + len(end_sig)

if start_idx != -1 and end_idx != -1:
    new_present = """STDMETHODIMP GLDirect3DDevice9::Present(CONST RECT* pSrc, CONST RECT* pDst, HWND hWnd, CONST RGNDATA* pReg) {
    if (g_sdlWindow) {
        static int frames = 0; if (++frames % 60 == 0) { printf("Present frame %d\\n", frames); fflush(stdout); }
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, 1024, 768);
        glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glUseProgram(0);
        
        // Draw FBO 1 texture as a full-screen quad
        glEnable(GL_TEXTURE_2D);
        // We need the texture ID. Let's assume it's attached to FBO 1.
        // Actually, we can just blit, it's easier if it works.
        // But let's try a LEGACY blit:
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 1);
        glBlitFramebuffer(0, 0, 1024, 768, 0, 0, 1024, 768, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        
        SDL_GL_SwapWindow((SDL_Window*)g_sdlWindow);
    }
    return D3D_OK;
}"""
    # Wait, I'll just use the already working blit but make sure it's not failing.
    # Actually, I'll use glBegin(GL_QUADS) with the texture!
    # But I need the texture ID.
    
    # I'll just use glBlitFramebuffer but with 1366x768 source!
    new_present = """STDMETHODIMP GLDirect3DDevice9::Present(CONST RECT* pSrc, CONST RECT* pDst, HWND hWnd, CONST RGNDATA* pReg) {
    if (g_sdlWindow) {
        static int frames = 0; if (++frames % 60 == 0) { printf("Present frame %d\\n", frames); fflush(stdout); }
        
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        for (GLuint fbo = 1; fbo <= 5; fbo++) {
            if (glIsFramebuffer(fbo)) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
                glBlitFramebuffer(0, 0, 1366, 768, 0, 0, 1024, 768, GL_COLOR_BUFFER_BIT, GL_LINEAR);
                glGetError();
            }
        }
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        SDL_GL_SwapWindow((SDL_Window*)g_sdlWindow);
    }
    return D3D_OK;
}"""
    content = content[:start_idx] + new_present + content[end_idx:]
    open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
    print("Present patched for 1366->1024 blit.")
else:
    print("Signatures not found!")

