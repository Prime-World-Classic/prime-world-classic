import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("GLint is3DLoc = glGetUniformLocation(m_shaderProg, \"is3D\");",
                          "GLint is3DLoc = glGetUniformLocation(m_shaderProg, \"is3D\"); static int draws=0; if(++draws < 100) { printf(\"Draw FBO=%u, stride=%u, fvf=%08X\\n\", g_currentFBO, m_streams[0].Stride, m_fvf); fflush(stdout); }")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
