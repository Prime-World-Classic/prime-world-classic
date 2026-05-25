import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("bool isRHW_flag = (f_ptr && (abs(f_ptr[0]) > 2.0f || abs(f_ptr[1]) > 2.0f));",
                          "float* f_ptr = NULL; if(m_streams[0].pStreamData) f_ptr = (float*)((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->m_pData; bool isRHW_flag = (f_ptr && (abs(f_ptr[0]) > 2.0f || abs(f_ptr[1]) > 2.0f));")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
