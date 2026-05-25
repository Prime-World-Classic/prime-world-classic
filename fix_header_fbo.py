import sys

with open("pw/branches/r1117/Src/Render/GLRenderer.h", "r") as f:
    lines = f.readlines()

new_lines = []
skip = False
for line in lines:
    if "GLuint GetFBO() {" in line:
        new_lines.append("    GLuint GetFBO();\n")
        skip = True
    elif skip and "}" in line:
        skip = False
    elif not skip:
        new_lines.append(line)

with open("pw/branches/r1117/Src/Render/GLRenderer.h", "w") as f:
    f.writelines(new_lines)
