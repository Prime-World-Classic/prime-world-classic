import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("NiBlitFramebuffer(0, 0, 1024, 768, 0, 768, 1024, 0, GL_COLOR_BUFFER_BIT, GL_LINEAR);",
                          "NiBlitFramebuffer(0, 0, 1024, 768, 0, 0, 1024, 768, GL_COLOR_BUFFER_BIT, GL_LINEAR);")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
