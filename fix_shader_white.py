import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("gl_FragColor = vec4(vColor.rgb * t0.rgb, 1.0);",
                          "gl_FragColor = (is3D == 0) ? vec4(1.0, 1.0, 1.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);")

content = content.replace("g_currentFBO = s->GetParent()->GetFBO(); glBindFramebuffer(GL_FRAMEBUFFER, g_currentFBO);",
                          "g_currentFBO = s->GetParent()->GetFBO(); glBindFramebuffer(GL_FRAMEBUFFER, g_currentFBO); glViewport(0, 0, s->GetParent()->GetWidth(), s->GetParent()->GetHeight());")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
