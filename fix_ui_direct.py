import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

old_draw = "glDrawElements(GetGLPrim(t), GetGLCount(t, pc), it, (void*)(uintptr_t)(si*is));"
new_draw = """if (isRHW_flag) glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawElements(GetGLPrim(t), GetGLCount(t, pc), it, (void*)(uintptr_t)(si*is));
    if (isRHW_flag) glBindFramebuffer(GL_FRAMEBUFFER, g_currentFBO);"""

content = content.replace(old_draw, new_draw)

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
