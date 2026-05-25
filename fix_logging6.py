import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("glGenFramebuffers(1, &m_fbo);", 
                          "glGenFramebuffers(1, &m_fbo); printf(\"FBO %u created\\n\", m_fbo); ")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
