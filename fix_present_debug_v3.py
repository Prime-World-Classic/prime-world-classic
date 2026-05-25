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
        
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        for (GLuint fbo = 1; fbo <= 5; fbo++) {
            if (glIsFramebuffer(fbo)) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
                glBlitFramebuffer(0, 0, 1024, 768, 0, 0, 1024, 768, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            }
        }
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, 1024, 768);
        glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glUseProgram(0);
        
        // CYAN TRIANGLE on top of blitted content
        glBegin(GL_TRIANGLES); glColor3f(0, 1, 1); glVertex2f(-0.3f, -0.3f); glVertex2f(0.3f, -0.3f); glVertex2f(0, 0.3f); glEnd();
        
        SDL_GL_SwapWindow((SDL_Window*)g_sdlWindow);
    }
    return D3D_OK;
}"""
    content = content[:start_idx] + new_present + content[end_idx:]
    open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
    print("Present patched for debug cyan triangle on top.")
