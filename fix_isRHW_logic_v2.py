import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("UpdateShaderProgram(isRHW_flag); ApplyAttributes(d, s, 0); glDrawElements",
                          "float* f = (float*)d; bool isRHW_flag = (f && (abs(f[0]) > 2.0f || abs(f[1]) > 2.0f)); UpdateShaderProgram(isRHW_flag); ApplyAttributes(d, s, 0); glDrawElements")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
