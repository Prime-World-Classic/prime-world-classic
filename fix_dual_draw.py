import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

old_draw = "glDrawElements(GetGLPrim(t), GetGLCount(t, pc), it, (void*)(uintptr_t)(si*is));"
new_draw = """glDrawElements(GetGLPrim(t), GetGLCount(t, pc), it, (void*)(uintptr_t)(si*is));
    if(g_currentFBO != 0) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glDrawElements(GetGLPrim(t), GetGLCount(t, pc), it, (void*)(uintptr_t)(si*is));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_currentFBO);
    }"""

if old_draw in content:
    content = content.replace(old_draw, new_draw)
    open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
    print("Dual draw enabled.")
else:
    print("Draw signature not found.")
