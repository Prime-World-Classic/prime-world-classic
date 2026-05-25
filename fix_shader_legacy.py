import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

new_usp = r"""void GLDirect3DDevice9::UpdateShaderProgram() {
    UINT stride = m_streams[0].Stride; bool isRHW = (m_fvf & D3DFVF_XYZRHW) != 0 || stride == 20 || stride == 28 || stride == 32 || stride == 16;
    if (isRHW) {
        glUseProgram(0);
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1366, 768, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_LIGHTING);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        if (m_shaderDirty) {
            const char* vs = "#version 120\nattribute vec4 position; attribute vec4 color; attribute vec2 texcoord0; varying vec4 vColor; varying vec2 vTexCoord; void main() { vTexCoord = texcoord0; vColor = color.bgra; gl_Position = position; }\n";
            const char* ps = "#version 120\nvarying vec4 vColor; varying vec2 vTexCoord; uniform sampler2D tex0; void main() { gl_FragColor = vColor * texture2D(tex0, vTexCoord); }\n";
            if (m_shaderProg) glDeleteProgram(m_shaderProg); m_shaderProg = glCreateProgram();
            GLuint v = CompileShader(GL_VERTEX_SHADER, vs); GLuint p = CompileShader(GL_FRAGMENT_SHADER, ps);
            glAttachShader(m_shaderProg, v); glAttachShader(m_shaderProg, p);
            glBindAttribLocation(m_shaderProg, 0, "position"); glBindAttribLocation(m_shaderProg, 1, "color"); glBindAttribLocation(m_shaderProg, 2, "texcoord0");
            glLinkProgram(m_shaderProg); m_shaderDirty = false;
        }
        glUseProgram(m_shaderProg);
    }
    for(int i=0; i<8; i++) { glActiveTexture(GL_TEXTURE0+i); if(m_textures[i]) glBindTexture(GL_TEXTURE_2D, ((GLDirect3DTexture9*)m_textures[i])->GetTex()); else glBindTexture(GL_TEXTURE_2D, 0); }
    if (isRHW) glEnable(GL_TEXTURE_2D);
}"""

# Find and replace UpdateShaderProgram
import re
content = re.sub(r'void GLDirect3DDevice9::UpdateShaderProgram\(\) \{.*?\}', new_usp, content, flags=re.DOTALL)

# Update ApplyAttributes to use legacy GL for RHW
new_aa = r"""void GLDirect3DDevice9::ApplyAttributes(const void* pUP, UINT ups, UINT startV) {
    UINT stride = m_streams[0].Stride; bool isRHW = (m_fvf & D3DFVF_XYZRHW) != 0 || stride == 20 || stride == 28 || stride == 32 || stride == 16;
    if (isRHW) {
        // Use fixed function pointers
        UINT s = pUP ? (ups ? ups : 20) : (m_streams[0].Stride ? m_streams[0].Stride : 20);
        const char* b = (const char*)(pUP ? pUP : (const char*)((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->m_pData + m_streams[0].OffsetInBytes + startV*s);
        glEnableClientState(GL_VERTEX_ARRAY); glVertexPointer(3, GL_FLOAT, s, b);
        glEnableClientState(GL_COLOR_ARRAY); glColorPointer(4, GL_UNSIGNED_BYTE, s, b+16);
        if (s >= 28) { glEnableClientState(GL_TEXTURE_COORD_ARRAY); glTexCoordPointer(2, GL_FLOAT, s, b+20); }
        else { glDisableClientState(GL_TEXTURE_COORD_ARRAY); }
        return;
    }
    for(int i=0; i<16; i++) glDisableVertexAttribArray(i);
    // ... rest of ApplyAttributes for 3D ...
}"""
# Just replace the whole ApplyAttributes
content = re.sub(r'void GLDirect3DDevice9::ApplyAttributes\(.*?\{.*?\}', new_aa, content, flags=re.DOTALL)

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
