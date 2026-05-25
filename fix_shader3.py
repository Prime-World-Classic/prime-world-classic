import sys

with open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "r") as f:
    content = f.read()

start_sig = "void GLDirect3DDevice9::UpdateShaderProgram() {"
end_sig = "void GLDirect3DDevice9::ApplyAttributes(const void* pUPData, UINT UPStride, UINT StartVertex) {"

start_idx = content.find(start_sig)
end_idx = content.find(end_sig)

if start_idx != -1 and end_idx != -1:
    new_func = """void GLDirect3DDevice9::UpdateShaderProgram() {
    if (m_shaderDirty) {
        const char* vsSource = 
            "#version 120\\n"
            "attribute vec4 position; attribute vec4 color; "
            "attribute vec2 texcoord0; attribute vec2 texcoord1; attribute vec2 texcoord2; attribute vec2 texcoord3;\\n"
            "varying vec4 vColor; varying vec2 vTexCoord;\\n"
            "uniform mat4 world; uniform mat4 view; uniform mat4 projection; uniform int is3D; uniform vec2 screenRes;\\n"
            "void main() {\\n"
            "  vTexCoord = texcoord0;\\n"
            "  if (is3D != 0 && abs(position.x) <= 150.0 && abs(position.y) <= 150.0) { \\n"
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
            "}\\n";
        if (m_shaderProg) glDeleteProgram(m_shaderProg); m_shaderProg = glCreateProgram();
        GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSource); GLuint ps = CompileShader(GL_FRAGMENT_SHADER, psSource);
        glAttachShader(m_shaderProg, vs); glAttachShader(m_shaderProg, ps);
        glBindAttribLocation(m_shaderProg, 0, "position"); glBindAttribLocation(m_shaderProg, 1, "color"); 
        glBindAttribLocation(m_shaderProg, 2, "texcoord0"); glBindAttribLocation(m_shaderProg, 3, "texcoord1"); 
        glBindAttribLocation(m_shaderProg, 4, "texcoord2"); glBindAttribLocation(m_shaderProg, 5, "texcoord3");
        glLinkProgram(m_shaderProg); GLint status; glGetProgramiv(m_shaderProg, GL_LINK_STATUS, &status);
        if (!status) { char info[512]; glGetProgramInfoLog(m_shaderProg, 512, NULL, info); fprintf(stderr, "Shader link error: %s\\n", info); }
        glDeleteShader(vs); glDeleteShader(ps); m_shaderDirty = false;
    }
    glUseProgram(m_shaderProg);
    
    GLint worldLoc = glGetUniformLocation(m_shaderProg, "world");
    if (worldLoc != -1) glUniformMatrix4fv(worldLoc, 1, GL_TRUE, (float*)&m_vsConstF[4]);

    GLint viewLoc = glGetUniformLocation(m_shaderProg, "view");
    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_TRUE, (float*)&m_vsConstF[8]);

    GLint projLoc = glGetUniformLocation(m_shaderProg, "projection");
    if (projLoc != -1) glUniformMatrix4fv(projLoc, 1, GL_TRUE, (float*)&m_vsConstF[12]);

    if (projLoc != -1 && m_vsConstF[12][0] == 0.0f) {
        glUniformMatrix4fv(projLoc, 1, GL_TRUE, (float*)&m_vsConstF[0]);
        static float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, identity);
        glUniformMatrix4fv(worldLoc, 1, GL_FALSE, identity);
    }

    GLint resLoc = glGetUniformLocation(m_shaderProg, "screenRes");
    float bw = m_presentParams.BackBufferWidth > 0 ? (float)m_presentParams.BackBufferWidth : 1024.0f;
    float bh = m_presentParams.BackBufferHeight > 0 ? (float)m_presentParams.BackBufferHeight : 768.0f;
    if (resLoc != -1) glUniform2f(resLoc, bw, bh);

    GLint is3DLoc = glGetUniformLocation(m_shaderProg, "is3D");
    bool hasPositionT = false;
    if (m_pVertexDecl) {
        const std::vector<D3DVERTEXELEMENT9>& elements = ((GLDirect3DVertexDeclaration9*)m_pVertexDecl)->GetElements();
        for (size_t i = 0; i < elements.size(); ++i) { if (elements[i].Usage == D3DDECLUSAGE_POSITIONT) hasPositionT = true; }
    }
    UINT stride = m_streams[0].Stride;
    bool isRHW = hasPositionT || (m_fvf & D3DFVF_XYZRHW) != 0 || stride == 20 || stride == 28;
    
    if (is3DLoc != -1) glUniform1i(is3DLoc, (m_pVertexShader || !isRHW) ? 1 : 0);
    
    if (isRHW) {
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_DEPTH_TEST);
    }

    GLint useTex0Loc = glGetUniformLocation(m_shaderProg, "useTex0"); if (useTex0Loc != -1) glUniform1i(useTex0Loc, m_textures[0] ? 1 : 0);
    for (int i = 0; i < 8; ++i) { 
        glActiveTexture(GL_TEXTURE0 + i); 
        if (m_textures[i]) { glBindTexture(GL_TEXTURE_2D, ((GLDirect3DTexture9*)m_textures[i])->GetTex()); } 
        else { glBindTexture(GL_TEXTURE_2D, 0); } 
        char name[8]; sprintf(name, "tex%d", i);
        GLint loc = glGetUniformLocation(m_shaderProg, name);
        if (loc != -1) glUniform1i(loc, i);
    }
}

"""
    new_content = content[:start_idx] + new_func + content[end_idx:]
    with open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w") as f:
        f.write(new_content)
    print("Shader patched successfully.")
else:
    print("Signatures not found!")

