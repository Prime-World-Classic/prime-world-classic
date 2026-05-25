import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

old_vs = """            "  if (is3D != 0) { \\n"
            "    vColor = vec4(0.0, 0.0, 0.0, 1.0); \\n" // Black for 3D
            "    gl_Position = projection * view * world * vec4(position.xyz, 1.0); \\n"
            "  } else { \\n"
            "    vColor = vec4(1.0, 1.0, 1.0, 1.0); \\n" // White for 2D
            "    float nx = (position.x / screenRes.x) * 2.0 - 1.0;\\n"
            "    float ny = 1.0 - (position.y / screenRes.y) * 2.0;\\n"
            "    gl_Position = vec4(nx, ny, 0.5, 1.0);\\n"
            "  }\\n"
            "}\\n";
        const char* psSource = 
            "#version 120\\n"
            "varying vec4 vColor; varying vec2 vTexCoord; uniform sampler2D tex0; uniform int useTex0;\\n"
            "void main() {\\n"
            "  vec4 t0 = useTex0 != 0 ? texture2D(tex0, vTexCoord) : vec4(1.0);\\n"
            "  gl_FragColor = vColor * t0; gl_FragColor.a = 1.0; \\n"
            "}\\n";"""

new_vs = """            "  if (is3D != 0 && abs(position.x) <= 150.0 && abs(position.y) <= 150.0) { \\n"
            "    vColor = color.bgra; if(vColor.a < 0.01) vColor.a = 1.0;\\n"
            "    gl_Position = projection * view * world * vec4(position.xyz, 1.0); \\n"
            "  } else { \\n"
            "    vColor = vec4(1.0, 1.0, 1.0, 1.0); \\n"
            "    float nx = (position.x / screenRes.x) * 2.0 - 1.0;\\n"
            "    float ny = 1.0 - (position.y / screenRes.y) * 2.0;\\n"
            "    gl_Position = vec4(nx, ny, 0.0, 1.0);\\n"
            "  }\\n"
            "}\\n";
        const char* psSource = 
            "#version 120\\n"
            "varying vec4 vColor; varying vec2 vTexCoord; uniform sampler2D tex0; uniform int useTex0;\\n"
            "void main() {\\n"
            "  vec4 t0 = useTex0 != 0 ? texture2D(tex0, vTexCoord) : vec4(1.0);\\n"
            "  if (useTex0 == 0) t0 = vec4(1.0, 0.0, 0.0, 1.0);\\n"
            "  gl_FragColor = vColor * t0; gl_FragColor.a = 1.0; \\n"
            "}\\n";"""

if old_vs in content:
    content = content.replace(old_vs, new_vs)
    print("Patched vsSource.")
else:
    print("vsSource not found.")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
