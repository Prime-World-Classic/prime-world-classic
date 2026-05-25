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
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 1);
        GLuint tex = 0;
        glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, (GLint*)&tex);
        
        if (tex > 0) {
            glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex);
            glColor4f(1, 1, 1, 1);
            glBegin(GL_QUADS);
            glTexCoord2f(0, 1); glVertex2f(-1, -1);
            glTexCoord2f(1, 1); glVertex2f(1, -1);
            glTexCoord2f(1, 0); glVertex2f(1, 1);
            glTexCoord2f(0, 0); glVertex2f(-1, 1);
            glEnd();
        } else {
            // If no texture, draw a small white triangle to know we tried
            glBegin(GL_TRIANGLES); glColor3f(1, 1, 1); glVertex2f(-0.1f, -0.1f); glVertex2f(0.1f, -0.1f); glVertex2f(0, 0.1f); glEnd();
        }
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        SDL_GL_SwapWindow((SDL_Window*)g_sdlWindow);
    }
    return D3D_OK;
}"""
    content = content[:start_idx] + new_present + content[end_idx:]
    open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
    print("Present patched for direct texture draw.")
else:
    print("Signatures not found!")

