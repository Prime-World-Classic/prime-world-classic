import sys

content = open("Src/Render/GLRenderer.cpp", "r").read()

content = content.replace(
    'gl_FragColor = vColor * t0;',
    'gl_FragColor = vec4(1.0);'
)

open("Src/Render/GLRenderer.cpp", "w").write(content)
