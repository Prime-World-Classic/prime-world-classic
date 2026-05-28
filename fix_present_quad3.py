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
        glClearColor(0, 0.2f, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
        
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
        glUseProgram(0);
        
        // Draw FBO 1 manually with a textured quad
        GLuint texToDraw = 0;
        GLint attachType;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 1);
        glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &attachType);
        if (attachType == GL_TEXTURE) {
            GLint attachName;
            glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &attachName);
            texToDraw = attachName;
        }
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, 1024, 768);
        
        if (texToDraw > 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texToDraw);
            glColor3f(1, 1, 1);
            glBegin(GL_QUADS);
            glTexCoord2f(0, 1); glVertex2f(-1, -1);
            glTexCoord2f(1, 1); glVertex2f(1, -1);
            glTexCoord2f(1, 0); glVertex2f(1, 1);
            glTexCoord2f(0, 0); glVertex2f(-1, 1);
            glEnd();
            glDisable(GL_TEXTURE_2D);
        } else {
             glBegin(GL_TRIANGLES); glColor3f(1, 0, 0); glVertex2f(-0.5f, -0.5f); glVertex2f(0.5f, -0.5f); glVertex2f(0, 0.5f); glEnd();
        }
        
        // ALSO try to blit it anyway, because maybe that worked better?
        // Actually blit FBO 2 just in case UI is there
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 2);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, 1024, 768, 0, 0, 1024, 768, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        SDL_GL_SwapWindow((SDL_Window*)g_sdlWindow);
    }
    return D3D_OK;
}"""
    content = content[:start_idx] + new_present + content[end_idx:]
    open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
    print("Present patched to draw FBO 1 texture as quad and blit FBO 2.")
else:
    print("Signatures not found!")
