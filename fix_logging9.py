import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("glGenFramebuffers(1, &m_fbo);", 
                          "glGenFramebuffers(1, &m_fbo); printf(\"FBO %u created for %ux%u\\n\", m_fbo, m_width, m_height); ")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
