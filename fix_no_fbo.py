import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("g_currentFBO = s->GetParent()->GetFBO();", "g_currentFBO = 0;")
content = content.replace("glBindFramebuffer(GL_FRAMEBUFFER, g_currentFBO);", "glBindFramebuffer(GL_FRAMEBUFFER, 0);")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
