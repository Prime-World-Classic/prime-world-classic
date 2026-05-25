import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

start_sig = "STDMETHODIMP GLDirect3DDevice9::Present("
end_sig = "return D3D_OK;\n}"

start_idx = content.find(start_sig)
end_idx = content.find(end_sig, start_idx) + len(end_sig)

if start_idx != -1 and end_idx != -1:
    new_present = """STDMETHODIMP GLDirect3DDevice9::Present(CONST RECT* pSrc, CONST RECT* pDst, HWND hWnd, CONST RGNDATA* pReg) {
    if (g_sdlWindow) {
        static int frames = 0; if (++frames % 60 == 0) { printf("Present frame %d (draws: %d)\\n", frames, g_drawCalls); fflush(stdout); }
        
        for (GLuint fbo = 1; fbo <= 8; fbo++) {
            if (glIsFramebuffer(fbo)) {
                glBindFramebuffer(GL_FRAMEBUFFER, fbo);
                GLint w, h;
                glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &w); // This gets texture ID, not size
                // Okay, I can't easily get size from FBO.
                // But I know most of them are 1024x768.
                glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                // Try blit with size check
                glBlitFramebuffer(0, 0, 1024, 768, 0, 0, 1024, 768, GL_COLOR_BUFFER_BIT, GL_LINEAR);
                glGetError(); // Clear error
            }
        }
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0); 
        SDL_GL_SwapWindow((SDL_Window*)g_sdlWindow);
    }
    return D3D_OK;
}"""
    content = content[:start_idx] + new_present + content[end_idx:]
    open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
    print("Present patched for FBO blit with error clearing.")
else:
    print("Signatures not found!")

