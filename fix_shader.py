import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

old_shader = """            "  vTexCoord = texcoord0;\\n"
            "  if (is3D != 0) { \\n"
            "    vColor = vec4(0.0, 0.0, 0.0, 1.0); \\n" // Black for 3D
            "    gl_Position = projection * view * world * vec4(position.xyz, 1.0); \\n"
            "  } else { \\n"
            "    vColor = vec4(1.0, 1.0, 1.0, 1.0); \\n" // White for 2D
            "    float nx = (position.x / screenRes.x) * 2.0 - 1.0;\\n"
            "    float ny = 1.0 - (position.y / screenRes.y) * 2.0;\\n"
            "    gl_Position = vec4(nx, ny, 0.5, 1.0);\\n"
            "  }\\n"
            "}\\n";"""

new_shader = """            "  vTexCoord = texcoord0;\\n"
            "  vColor = color.bgra;\\n"
            "  if (vColor.a < 0.01) vColor.a = 1.0;\\n"
            "  if (is3D != 0 && abs(position.x) <= 150.0 && abs(position.y) <= 150.0) { \\n"
            "    gl_Position = projection * view * world * vec4(position.xyz, 1.0); \\n"
            "  } else { \\n"
            "    float nx = (position.x / screenRes.x) * 2.0 - 1.0;\\n"
            "    float ny = 1.0 - (position.y / screenRes.y) * 2.0;\\n"
            "    gl_Position = vec4(nx, ny, 0.0, 1.0);\\n"
            "  }\\n"
            "}\\n";"""

old_ps = """        const char* psSource = 
            "#version 120\\n"
            "varying vec4 vColor; varying vec2 vTexCoord; uniform sampler2D tex0; uniform int useTex0;\\n"
            "void main() {\\n"
            "  vec4 t0 = useTex0 != 0 ? texture2D(tex0, vTexCoord) : vec4(1.0);\\n"
            "  gl_FragColor = vColor * t0; gl_FragColor.a = 1.0; \\n"
            "}\\n";"""

new_ps = """        const char* psSource = 
            "#version 120\\n"
            "varying vec4 vColor; varying vec2 vTexCoord; uniform sampler2D tex0; uniform int useTex0;\\n"
            "void main() {\\n"
            "  vec4 t0 = useTex0 != 0 ? texture2D(tex0, vTexCoord) : vec4(1.0);\\n"
            "  gl_FragColor = vColor * t0;\\n"
            "}\\n";"""

if old_shader in content:
    content = content.replace(old_shader, new_shader)
    print("Replaced vsSource.")
if old_ps in content:
    content = content.replace(old_ps, new_ps)
    print("Replaced psSource.")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
