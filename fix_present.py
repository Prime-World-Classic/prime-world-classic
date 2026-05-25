import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

old_present = """STDMETHODIMP GLDirect3DDevice9::Present(CONST RECT* pSrc, CONST RECT* pDst, HWND hWnd, CONST RGNDATA* pReg) {
    if (g_sdlWindow) {
        static int frames = 0; if (++frames % 60 == 0) { printf("Present frame %d (draws: %d)\\n", frames, g_drawCalls); fflush(stdout); }
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, 1024, 768); 
        // Force magenta triangle
        glDisable(GL_DEPTH_TEST); glUseProgram(0); glBegin(GL_TRIANGLES); glColor3f(1,0,1); glVertex2f(-0.5f,-0.5f); glVertex2f(0.5f,-0.5f); glVertex2f(0,0.5f); glEnd();
        SDL_GL_SwapWindow((SDL_Window*)g_sdlWindow);
    }
    return D3D_OK;
}"""

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
        // but let's keep a tiny red pixel in corner to know it's alive
        glDisable(GL_DEPTH_TEST); glUseProgram(0); glBegin(GL_POINTS); glColor3f(1,0,0); glVertex2f(-0.99f, -0.99f); glEnd();

        SDL_GL_SwapWindow((SDL_Window*)g_sdlWindow);
    }
    return D3D_OK;
}"""

if old_present in content:
    content = content.replace(old_present, new_present)
    print("Present patched.")
else:
    print("Present not found.")

old_heur = "UINT stride = m_streams[0].Stride; bool isRHW = (m_fvf & D3DFVF_XYZRHW) != 0 || stride == 20 || stride == 28;"
new_heur = "UINT stride = m_streams[0].Stride; bool isRHW = (m_fvf & D3DFVF_XYZRHW) != 0 || stride == 20 || stride == 28 || stride == 32 || stride == 16;"

if old_heur in content:
    content = content.replace(old_heur, new_heur)
    print("Heuristic patched.")
else:
    print("Heuristic not found.")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
