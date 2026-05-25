import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("gl_FragColor = vec4(vColor.rgb, 1.0);",
                          "gl_FragColor = (is3D == 0) ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
