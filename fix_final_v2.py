import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

new_shader = r"""void GLDirect3DDevice9::UpdateShaderProgram() {
    UINT stride = m_streams[0].Stride;
    float* f = NULL; if(m_streams[0].pStreamData) f = (float*)((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->m_pData;
    bool isRHW = (f && (abs(f[0]) > 2.0f || abs(f[1]) > 2.0f));
    
    if (m_shaderDirty) {
        const char* vs = "#version 120\nattribute vec4 position; attribute vec4 color; attribute vec2 texcoord0; varying vec4 vColor; varying vec2 vTexCoord; uniform int is3D; uniform vec2 screenRes;\nvoid main() {\nvTexCoord = texcoord0; vColor = color.bgra; if(vColor.a < 0.01) vColor.a = 1.0;\nif (is3D != 0) { gl_Position = vec4(position.xyz, 1.0); }\nelse { gl_Position = vec4((position.x/1366.0)*2.0-1.0, 1.0-(position.y/768.0)*2.0, 0.0, 1.0); }\n}\n";
        const char* ps = "#version 120\nvarying vec4 vColor; varying vec2 vTexCoord; uniform sampler2D tex0; uniform int useTex0;\nvoid main() {\nvec4 t0 = useTex0 != 0 ? texture2D(tex0, vTexCoord) : vec4(1.0);\ngl_FragColor = vColor * t0;\n}\n";
        if (m_shaderProg) glDeleteProgram(m_shaderProg); m_shaderProg = glCreateProgram();
        GLuint v = CompileShader(GL_VERTEX_SHADER, vs); GLuint p = CompileShader(GL_FRAGMENT_SHADER, ps);
        glAttachShader(m_shaderProg, v); glAttachShader(m_shaderProg, p);
        glBindAttribLocation(m_shaderProg, 0, "position"); glBindAttribLocation(m_shaderProg, 1, "color"); glBindAttribLocation(m_shaderProg, 2, "texcoord0");
        glLinkProgram(m_shaderProg); m_shaderDirty = false;
    }
    glUseProgram(m_shaderProg);
    GLint is3DLoc = glGetUniformLocation(m_shaderProg, "is3D"); if (is3DLoc != -1) glUniform1i(is3DLoc, isRHW ? 0 : 1);
    GLint resLoc = glGetUniformLocation(m_shaderProg, "screenRes"); if (resLoc != -1) glUniform2f(resLoc, 1024.f, 768.f);
    GLint useTexLoc = glGetUniformLocation(m_shaderProg, "useTex0"); if (useTexLoc != -1) glUniform1i(useTexLoc, m_textures[0] ? 1 : 0);
    for(int i=0; i<8; i++) { glActiveTexture(GL_TEXTURE0+i); if(m_textures[i]) glBindTexture(GL_TEXTURE_2D, ((GLDirect3DTexture9*)m_textures[i])->GetTex()); else glBindTexture(GL_TEXTURE_2D, 0); }
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (isRHW) { glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); } else { glEnable(GL_DEPTH_TEST); }
}"""

import re
content = re.sub(r'void GLDirect3DDevice9::UpdateShaderProgram\(\) \{.*?\}', new_shader, content, flags=re.DOTALL)

# Re-enable FBOs
content = content.replace("g_currentFBO = 0;", "g_currentFBO = s->GetParent()->GetFBO();")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
