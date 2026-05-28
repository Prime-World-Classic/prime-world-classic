import sys

with open("Src/Render/GLRenderer.cpp", "r") as f:
    content = f.read()

# Fix Present
content = content.replace(
    'STDMETHODIMP GLDirect3DDevice9::Present(CONST RECT* pSrc, CONST RECT* pDst, HWND hWnd, CONST RGNDATA* pReg) {\n    if (g_sdlWindow) {\n        // Blit FBOs 1 and 2 to backbuffer\n        for (GLuint fbo = 1; fbo <= 5; fbo++) {\n            if (glIsFramebuffer(fbo)) {\n                glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);\n                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);\n                glBlitFramebuffer(0, 0, 1024, 768, 0, 0, 1024, 768, GL_COLOR_BUFFER_BIT, GL_NEAREST);\n            }\n        }\n        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);\n        glClearColor(0, 0.2f, 0, 1); glClear(GL_COLOR_BUFFER_BIT); // Dark green background\n        static int frames = 0; if (++frames % 60 == 0) { printf("Present frame %d\\n", frames); fflush(stdout); }\n        SDL_GL_SwapWindow((SDL_Window*)g_sdlWindow);\n    }\n    return D3D_OK;\n}',
    'STDMETHODIMP GLDirect3DDevice9::Present(CONST RECT* pSrc, CONST RECT* pDst, HWND hWnd, CONST RGNDATA* pReg) {\n    if (g_sdlWindow) {\n        static int frames = 0; if (++frames % 60 == 0) { printf("Present frame %d\\n", frames); fflush(stdout); }\n        SDL_GL_SwapWindow((SDL_Window*)g_sdlWindow);\n    }\n    return D3D_OK;\n}'
)

# Fix UpdateShaderProgram
content = content.replace(
    'void GLDirect3DDevice9::UpdateShaderProgram(bool isRHW) {\n    if (isRHW) { glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST); } else { glEnable(GL_DEPTH_TEST); }\n    if (m_shaderDirty) {\n        const char* vs = "#version 120\\nattribute vec4 position; attribute vec4 color; attribute vec2 texcoord0; varying vec4 vColor; varying vec2 vTexCoord; uniform int is3D; uniform vec2 screenRes; uniform vec4 vc[256];\\nvoid main() {\\nvTexCoord = texcoord0; vColor = color.bgra; if(vColor.a < 0.01) vColor.a = 1.0;\\nif (is3D != 0) { gl_Position = vec4(dot(position, vc[0]), dot(position, vc[1]), dot(position, vc[2]), dot(position, vc[3])); }\\nelse { gl_Position = vec4((position.x/1024.0)*2.0-1.0, 1.0-(position.y/768.0)*2.0, position.z, 1.0); }\\n}\\n";\n        const char* ps = "#version 120\\nvarying vec4 vColor; varying vec2 vTexCoord; uniform sampler2D tex0; uniform int useTex0;\\nvoid main() {\\nvec4 t0 = useTex0 != 0 ? texture2D(tex0, vTexCoord) : vec4(1.0);\\ngl_FragColor = vColor * t0;\\n}\\n";',
    'void GLDirect3DDevice9::UpdateShaderProgram(bool isRHW) {\n    if (isRHW) { glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_ALPHA_TEST); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); }\n    if (m_shaderDirty) {\n        const char* vs = "#version 120\\nattribute vec4 position; attribute vec4 color; attribute vec2 texcoord0; varying vec4 vColor; varying vec2 vTexCoord; uniform int is3D; uniform vec2 screenRes; uniform vec4 vc[256];\\nvoid main() {\\nvTexCoord = texcoord0; vColor = color.bgra; if(vColor.a < 0.01) vColor.a = 1.0;\\nif (is3D != 0) { gl_Position = vec4(dot(position, vc[0]), dot(position, vc[1]), dot(position, vc[2]), dot(position, vc[3])); }\\nelse { gl_Position = vec4((position.x/1024.0)*2.0-1.0, 1.0-(position.y/768.0)*2.0, 0.0, 1.0); }\\n}\\n";\n        const char* ps = "#version 120\\nvarying vec4 vColor; varying vec2 vTexCoord; uniform sampler2D tex0; uniform int useTex0;\\nvoid main() {\\nvec4 t0 = useTex0 != 0 ? texture2D(tex0, vTexCoord) : vec4(1.0);\\ngl_FragColor = vColor * t0;\\n}\\n";'
)

# Fix FBO completeness by adding a depth buffer
content = content.replace(
    'glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);',
    'glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);\n        GLuint rbo;\n        glGenRenderbuffers(1, &rbo);\n        glBindRenderbuffer(GL_RENDERBUFFER, rbo);\n        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);\n        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);'
)

# Fix IsRHW to correctly detect UI elements via VDecl
content = content.replace(
    'bool GLDirect3DDevice9::IsRHW(const void* d) {\n    if (m_fvf & D3DFVF_XYZRHW) return true;\n    if (!d) return false;\n    float* f_ptr = (float*)d;\n    return (abs(f_ptr[0]) > 2.0f || abs(f_ptr[1]) > 2.0f);\n}',
    'bool GLDirect3DDevice9::IsRHW(const void* d) {\n    if (m_fvf & D3DFVF_XYZRHW) return true;\n    if (m_pVertexDecl) {\n        const auto& els = ((GLDirect3DVertexDeclaration9*)m_pVertexDecl)->GetElements();\n        for (const auto& e : els) {\n            if (e.Usage == 9 /*D3DDECLUSAGE_POSITIONT*/) return true;\n            if (e.Usage == 0 /*D3DDECLUSAGE_POSITION*/ && e.Type == 1 /*D3DDECLTYPE_FLOAT2*/) return true;\n        }\n    }\n    if (!d) return false;\n    float* f_ptr = (float*)d;\n    return (abs(f_ptr[0]) > 2.0f || abs(f_ptr[1]) > 2.0f);\n}'
)

with open("Src/Render/GLRenderer.cpp", "w") as f:
    f.write(content)

print("Modifications applied successfully via python!")
