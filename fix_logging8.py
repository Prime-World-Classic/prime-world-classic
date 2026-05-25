import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("printf(\"Draw FBO=%u, stride=%u, fvf=%08X\\n\", g_currentFBO, m_streams[0].Stride, m_fvf);",
                          "if(m_streams[0].pStreamData) { float* f = (float*)((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->m_pData; printf(\"Draw FBO=%u, stride=%u, v=(%.2f, %.2f)\\n\", g_currentFBO, m_streams[0].Stride, f[0], f[1]); }")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
