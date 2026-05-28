import sys

with open("Src/Render/GLRenderer.cpp", "r") as f:
    content = f.read()

def replace_exact_once(content, old, new, desc):
    if content.count(old) != 1:
        print(f"Error: Could not find exactly 1 occurrence of {desc} (Found {content.count(old)})")
        sys.exit(1)
    return content.replace(old, new)

old_present = """        // Blit FBOs 1 and 2 to backbuffer
        for (GLuint fbo = 1; fbo <= 5; fbo++) {
            if (glIsFramebuffer(fbo)) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                glBlitFramebuffer(0, 0, 1024, 768, 0, 0, 1024, 768, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            }
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glClearColor(0, 0.2f, 0, 1); glClear(GL_COLOR_BUFFER_BIT); // Dark green background"""
content = replace_exact_once(content, old_present, "", "Present blit/clear hack")

old_update_1 = "    if (isRHW) { glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST); } else { glEnable(GL_DEPTH_TEST); }"
new_update_1 = "    if (isRHW) { glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_ALPHA_TEST); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); } else { glEnable(GL_DEPTH_TEST); }"
content = replace_exact_once(content, old_update_1, new_update_1, "UpdateShaderProgram states")

old_update_2 = """        const char* vs = "#version 120\\nattribute vec4 position; attribute vec4 color; attribute vec2 texcoord0; varying vec4 vColor; varying vec2 vTexCoord; uniform int is3D; uniform vec2 screenRes; uniform vec4 vc[256];\\nvoid main() {\\nvTexCoord = texcoord0; vColor = color.bgra;\\nif (is3D != 0) { gl_Position = vec4(dot(position, vc[0]), dot(position, vc[1]), dot(position, vc[2]), dot(position, vc[3])); }\\nelse { gl_Position = vec4((position.x/1024.0)*2.0-1.0, 1.0-(position.y/768.0)*2.0, position.z, 1.0); }\\n}\\n";"""
new_update_2 = """        const char* vs = "#version 120\\nattribute vec4 position; attribute vec4 color; attribute vec2 texcoord0; varying vec4 vColor; varying vec2 vTexCoord; uniform int is3D; uniform vec2 screenRes; uniform vec4 vc[256];\\nvoid main() {\\nvTexCoord = texcoord0; vColor = color.bgra; if(vColor.a < 0.01) vColor.a = 1.0;\\nif (is3D != 0) { gl_Position = vec4(dot(position, vc[0]), dot(position, vc[1]), dot(position, vc[2]), dot(position, vc[3])); }\\nelse { gl_Position = vec4((position.x/1024.0)*2.0-1.0, 1.0-(position.y/768.0)*2.0, 0.0, 1.0); }\\n}\\n";"""
content = replace_exact_once(content, old_update_2, new_update_2, "UpdateShaderProgram vertex shader")

old_apply = """void GLDirect3DDevice9::ApplyAttributes(const void* pUP, UINT ups, UINT startV) {
    if (m_pVertexDecl && !pUP) {"""
new_apply = """void GLDirect3DDevice9::ApplyAttributes(const void* pUP, UINT ups, UINT startV) {
    glVertexAttrib4f(1, 1.0f, 1.0f, 1.0f, 1.0f);
    if (m_pVertexDecl && !pUP) {"""
content = replace_exact_once(content, old_apply, new_apply, "ApplyAttributes default color")

old_create = """void GLDirect3DTexture9::Create(UINT w, UINT h, D3DFORMAT f, DWORD u, D3DPOOL p) {
    m_width = w; m_height = h; m_format = f; m_usage = u; m_pool = p;
    if (u & D3DUSAGE_RENDERTARGET) {
        glGenFramebuffers(1, &m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glGenTextures(1, &m_tex);
        glBindTexture(GL_TEXTURE_2D, m_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, g_currentFBO);
    }"""
new_create = """void GLDirect3DTexture9::Create(UINT w, UINT h, D3DFORMAT f, DWORD u, D3DPOOL p) {
    m_width = w; m_height = h; m_format = f; m_usage = u; m_pool = p;
    if (u & D3DUSAGE_RENDERTARGET) {
        glGenFramebuffers(1, &m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glGenTextures(1, &m_tex);
        glBindTexture(GL_TEXTURE_2D, m_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);
        
        GLuint rbo;
        glGenRenderbuffers(1, &rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);
        
        glBindFramebuffer(GL_FRAMEBUFFER, g_currentFBO);
    }"""
content = replace_exact_once(content, old_create, new_create, "Create FBO depth attachment")

old_isrhw = """bool GLDirect3DDevice9::IsRHW(const void* d) {
    if (m_fvf & D3DFVF_XYZRHW) return true;
    if (!d) return false;
    float* f_ptr = (float*)d;
    return (abs(f_ptr[0]) > 2.0f || abs(f_ptr[1]) > 2.0f);
}"""
new_isrhw = """bool GLDirect3DDevice9::IsRHW(const void* d) {
    if (m_fvf & D3DFVF_XYZRHW) return true;
    if (m_pVertexDecl) {
        const auto& els = ((GLDirect3DVertexDeclaration9*)m_pVertexDecl)->GetElements();
        for (const auto& e : els) {
            if (e.Usage == 9 /*D3DDECLUSAGE_POSITIONT*/) return true;
            if (e.Usage == 0 /*D3DDECLUSAGE_POSITION*/ && e.Type == 1 /*D3DDECLTYPE_FLOAT2*/) return true;
        }
    }
    if (!d) return false;
    float* f_ptr = (float*)d;
    return (abs(f_ptr[0]) > 2.0f || abs(f_ptr[1]) > 2.0f);
}"""
content = replace_exact_once(content, old_isrhw, new_isrhw, "IsRHW detection")

with open("Src/Render/GLRenderer.cpp", "w") as f:
    f.write(content)

print("All modifications successfully applied with strict verification.")
