import re

path = "pw/branches/r1117/Src/Render/GLRenderer.cpp"
with open(path, "r") as f:
    text = f.read()

# 1. Update GLDirect3DTexture9 to handle more formats and correct memory allocation
texture_impl = r"""
static UINT GetFormatBPP(D3DFORMAT fmt) {
    switch (fmt) {
        case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: return 4;
        case D3DFMT_A16B16G16R16F: return 8;
        case D3DFMT_A32B32G32R32F: return 16;
        case D3DFMT_R32F: return 4;
        case D3DFMT_R16F: return 2;
        case D3DFMT_L8: case D3DFMT_A8: return 1;
        case D3DFMT_L16: return 2;
        case 827611204: return 0; // DXT1
        case 894720068: return 0; // DXT5
        default: return 4;
    }
}

GLDirect3DTexture9::GLDirect3DTexture9(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool) : m_tex(0), m_width(Width), m_height(Height), m_format(Format), m_levels(Levels), m_pData(NULL) {
    m_refCount = 1; 
    UINT bpp = GetFormatBPP(Format);
    if (bpp > 0) m_pData = malloc(Width * Height * bpp);
    else m_pData = malloc(Width * Height); // Dummy for compressed
    glGenTextures(1, &m_tex); glBindTexture(GL_TEXTURE_2D, m_tex); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

GLDirect3DTexture9::~GLDirect3DTexture9() { if (m_pData) free(m_pData); if (m_tex) glDeleteTextures(1, &m_tex); }

STDMETHODIMP GLDirect3DTexture9::LockRect(UINT Level, D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags) { 
    if (pLockedRect) { 
        UINT bpp = GetFormatBPP(m_format);
        pLockedRect->Pitch = m_width * bpp; 
        pLockedRect->pBits = m_pData; 
    } 
    return D3D_OK; 
}

STDMETHODIMP GLDirect3DTexture9::UnlockRect(UINT Level) {
    glBindTexture(GL_TEXTURE_2D, m_tex);
    if (m_format == 827611204) glCompressedTexImage2D(GL_TEXTURE_2D, Level, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, m_width, m_height, 0, std::max(1u, m_width/4) * std::max(1u, m_height/4) * 8, m_pData);
    else if (m_format == 894720068) glCompressedTexImage2D(GL_TEXTURE_2D, Level, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, m_width, m_height, 0, std::max(1u, m_width/4) * std::max(1u, m_height/4) * 16, m_pData);
    else if (m_format == D3DFMT_A32B32G32R32F) glTexImage2D(GL_TEXTURE_2D, Level, GL_RGBA32F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, m_pData);
    else if (m_format == D3DFMT_A16B16G16R16F) glTexImage2D(GL_TEXTURE_2D, Level, GL_RGBA16F, m_width, m_height, 0, GL_RGBA, GL_HALF_FLOAT, m_pData);
    else if (m_format == D3DFMT_L8) glTexImage2D(GL_TEXTURE_2D, Level, GL_LUMINANCE, m_width, m_height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, m_pData);
    else glTexImage2D(GL_TEXTURE_2D, Level, GL_RGBA, m_width, m_height, 0, GL_BGRA, GL_UNSIGNED_BYTE, m_pData);
    return D3D_OK;
}
"""

# 2. Replace old GLDirect3DTexture9 implementation
text = re.sub(r'GLDirect3DTexture9::GLDirect3DTexture9[\s\S]*?return D3D_OK;\n}', texture_impl, text)

# 3. Fix shader to support alpha blending correctly
text = text.replace(r'gl_FragColor = vec4(vColor.rgb * t0.rgb, 1.0);', r'gl_FragColor = vColor * t0;')

with open(path, "w") as f:
    f.write(text)
