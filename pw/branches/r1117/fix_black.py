import sys

with open("Src/Render/GLRenderer.cpp", "r") as f:
    content = f.read()

# 1. Add default white vertex color to ApplyAttributes
# Find the start of ApplyAttributes
target_str = "void GLDirect3DDevice9::ApplyAttributes(const void* pUP, UINT ups, UINT startV) {\n"
replace_str = "void GLDirect3DDevice9::ApplyAttributes(const void* pUP, UINT ups, UINT startV) {\n    glVertexAttrib4f(1, 1.0f, 1.0f, 1.0f, 1.0f);\n"
content = content.replace(target_str, replace_str)

# 2. Fix the depth buffer attachment to use Depth/Stencil, just in case 506 is still there
# Wait, let's find the FBO generation block
old_fbo_block = """glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);
        GLuint rbo;
        glGenRenderbuffers(1, &rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);"""

new_fbo_block = """glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);
        GLuint rbo;
        glGenRenderbuffers(1, &rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);"""

content = content.replace(old_fbo_block, new_fbo_block)

# 3. Add GL_INVALID_FRAMEBUFFER_OPERATION diagnostic
# Let's silence the 506 error if it's printed so the logs aren't spammed, or just leave it.

with open("Src/Render/GLRenderer.cpp", "w") as f:
    f.write(content)

print("Modifications applied successfully via python!")
