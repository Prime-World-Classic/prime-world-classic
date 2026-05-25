import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

old_draw = "glDrawElements(GetGLPrim(t), GetGLCount(t, pc), it, (void*)(uintptr_t)(si*is));"
new_draw = """glDrawElements(GetGLPrim(t), GetGLCount(t, pc), it, (void*)(uintptr_t)(si*is));
    // DEBUG DOT
    glUseProgram(0); glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBegin(GL_POINTS); glColor3f(1,1,1); glVertex2f(-0.9f + (float)g_drawCalls*0.01f, -0.9f); glEnd();
    glBindFramebuffer(GL_FRAMEBUFFER, g_currentFBO);"""

# Only replace the first occurrence (main draw)
content = content.replace(old_draw, new_draw, 1)

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
