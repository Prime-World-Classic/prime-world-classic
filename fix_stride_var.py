import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("bool isRHW_flag = (s_tmp == 32 || s_tmp == 16 || (f_ptr && (abs(f_ptr[0]) > 2.0f || abs(f_ptr[1]) > 2.0f)));",
                          "bool isRHW_flag = (m_fvf & D3DFVF_XYZRHW) || (f_ptr && (abs(f_ptr[0]) > 2.0f || abs(f_ptr[1]) > 2.0f));")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
