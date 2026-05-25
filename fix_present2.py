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
        
        // BLIT FBO 1 (UI) to FBO 0 (Backbuffer)
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 1);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        NiBlitFramebuffer(0, 0, 1024, 768, 0, 768, 1024, 0, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0); 
        glViewport(0, 0, 1024, 768); 
        
        // No more magenta triangle, we want to see the UI!
        glDisable(GL_DEPTH_TEST); glUseProgram(0); glBegin(GL_POINTS); glColor3f(1,0,0); glVertex2f(-0.99f, -0.99f); glEnd();

        SDL_GL_SwapWindow((SDL_Window*)g_sdlWindow);
    }
    return D3D_OK;
}"""
    content = content[:start_idx] + new_present + content[end_idx:]
    open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
    print("Present patched successfully.")
else:
    print("Signatures not found!")

