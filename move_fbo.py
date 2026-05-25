import sys

# Read header and cpp
header = open("pw/branches/r1117/Src/Render/GLRenderer.h").read()
cpp = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

# Implementation of GetFBO
get_fbo_impl = """
GLuint GLDirect3DTexture9::GetFBO() { 
    if (!m_fbo) { 
        glGenFramebuffers(1, &m_fbo); 
        printf(\"FBO %u created for tex %ux%u\\n\", m_fbo, m_width, m_height); fflush(stdout);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    return m_fbo;
}
"""

# Remove from header
header = header.replace("GLuint GetFBO() { if (!m_fbo) { glGenFramebuffers(1, &m_fbo); glBindFramebuffer(GL_FRAMEBUFFER, m_fbo); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0); glBindFramebuffer(GL_FRAMEBUFFER, 0); } return m_fbo; }",
                        "GLuint GetFBO();")

# Add to cpp
cpp += get_fbo_impl

with open("pw/branches/r1117/Src/Render/GLRenderer.h", "w") as f: f.write(header)
with open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w") as f: f.write(cpp)
print("Moved GetFBO to cpp.")
