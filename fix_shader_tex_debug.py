import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("vColor = color.bgra; if(vColor.a < 0.01) vColor.a = 1.0;",
                          "vColor = vec4(texcoord0, 0.0, 1.0);")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
