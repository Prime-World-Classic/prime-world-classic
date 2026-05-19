import re

path = "pw/branches/r1117/Src/Render/GLRenderer.cpp"
with open(path, "r") as f:
    text = f.read()

# 1. Implementation of GLDirect3DSurface9
surface_impl = """
class GLDirect3DTexture9;
class GLDirect3DSurface9 : public IDirect3DSurface9 {
    UINT m_width, m_height;
    D3DFORMAT m_format;
    GLDirect3DTexture9* m_pParent;
    UINT m_level;
public:
    GLDirect3DSurface9(UINT w=0, UINT h=0, D3DFORMAT fmt=D3DFMT_UNKNOWN, GLDirect3DTexture9* parent=NULL, UINT level=0) 
        : m_width(w), m_height(h), m_format(fmt), m_pParent(parent), m_level(level) {}
    STDMETHOD_(ULONG, AddRef)() { return 1; }
    STDMETHOD_(ULONG, Release)() { return 1; }
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) { return E_NOINTERFACE; }
    STDMETHOD(GetDevice)(IDirect3DDevice9** ppDevice) { return E_NOTIMPL; }
    STDMETHOD(SetPrivateData)(REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags) { return E_NOTIMPL; }
    STDMETHOD(GetPrivateData)(REFGUID refguid, void* pData, DWORD* pSizeOfData) { return E_NOTIMPL; }
    STDMETHOD(FreePrivateData)(REFGUID refguid) { return E_NOTIMPL; }
    STDMETHOD_(DWORD, SetPriority)(DWORD PriorityNew) { return 0; }
    STDMETHOD_(DWORD, GetPriority)() { return 0; }
    STDMETHOD_(void, PreLoad)() {}
    STDMETHOD_(D3DRESOURCETYPE, GetType)() { return D3DRTYPE_SURFACE; }
    STDMETHOD(GetContainer)(REFIID riid, void** ppContainer) { return E_NOTIMPL; }
    STDMETHOD(GetDesc)(D3DSURFACE_DESC* pDesc) { 
        if (pDesc) { pDesc->Format = m_format; pDesc->Type = D3DRTYPE_SURFACE; pDesc->Usage = 0; pDesc->Pool = D3DPOOL_DEFAULT; pDesc->MultiSampleType = D3DMULTISAMPLE_NONE; pDesc->MultiSampleQuality = 0; pDesc->Width = m_width; pDesc->Height = m_height; }
        return D3D_OK; 
    }
    STDMETHOD(LockRect)(D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags);
    STDMETHOD(UnlockRect)();
    STDMETHOD(GetDC)(HDC* phdc) { return E_NOTIMPL; }
    STDMETHOD(ReleaseDC)(HDC hdc) { return E_NOTIMPL; }
};
"""

# 2. Implementation of GLDirect3DTexture9 with GetSurfaceLevel
texture_impl = """
class GLDirect3DTexture9 : public IDirect3DTexture9 {
    GLuint m_tex;
    UINT m_width, m_height, m_levels;
    D3DFORMAT m_format;
    void* m_pData;
    LONG m_refCount;
public:
    GLDirect3DTexture9(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool);
    ~GLDirect3DTexture9();
    STDMETHOD_(ULONG, AddRef)() { return InterlockedIncrement(&m_refCount); }
    STDMETHOD_(ULONG, Release)() { LONG res = InterlockedDecrement(&m_refCount); if (res==0) delete this; return res; }
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) { return E_NOINTERFACE; }
    STDMETHOD(GetDevice)(IDirect3DDevice9** ppDevice) { return E_NOTIMPL; }
    STDMETHOD(SetPrivateData)(REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags) { return E_NOTIMPL; }
    STDMETHOD(GetPrivateData)(REFGUID refguid, void* pData, DWORD* pSizeOfData) { return E_NOTIMPL; }
    STDMETHOD(FreePrivateData)(REFGUID refguid) { return E_NOTIMPL; }
    STDMETHOD_(DWORD, SetPriority)(DWORD PriorityNew) { return 0; }
    STDMETHOD_(DWORD, GetPriority)() { return 0; }
    STDMETHOD_(void, PreLoad)() {}
    STDMETHOD_(D3DRESOURCETYPE, GetType)() { return D3DRTYPE_TEXTURE; }
    STDMETHOD_(DWORD, GetLevelCount)() { return m_levels; }
    STDMETHOD(SetLOD)(DWORD LODNew) { return D3D_OK; }
    STDMETHOD_(DWORD, GetLOD)() { return 0; }
    STDMETHOD(GetLevelDesc)(UINT Level, D3DSURFACE_DESC* pDesc) { if (pDesc) { pDesc->Format = m_format; pDesc->Type = D3DRTYPE_SURFACE; pDesc->Width = m_width; pDesc->Height = m_height; } return D3D_OK; }
    STDMETHOD(GetSurfaceLevel)(UINT Level, IDirect3DSurface9** ppSurfaceLevel) {
        if (ppSurfaceLevel) { *ppSurfaceLevel = new GLDirect3DSurface9(m_width, m_height, m_format, this, Level); }
        return D3D_OK;
    }
    STDMETHOD(LockRect)(UINT Level, D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags);
    STDMETHOD(UnlockRect)(UINT Level);
    STDMETHOD(AddDirtyRect)(CONST RECT* pDirtyRect) { return D3D_OK; }
    GLuint GetTex() const { return m_tex; }
};

STDMETHODIMP GLDirect3DSurface9::LockRect(D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags) { 
    if (m_pParent) return m_pParent->LockRect(m_level, pLockedRect, pRect, Flags);
    return E_NOTIMPL;
}
STDMETHODIMP GLDirect3DSurface9::UnlockRect() { 
    if (m_pParent) return m_pParent->UnlockRect(m_level);
    return E_NOTIMPL;
}
"""

# Remove old partial implementations
text = re.sub(r'class GLDirect3DSurface9[\s\S]*?};', '', text)
text = re.sub(r'class GLDirect3DTexture9[\s\S]*?};', '', text)

# Insert new implementations after Device class or before VertexDeclaration
text = text.replace("class GLDirect3DVertexDeclaration9", surface_impl + "\n" + texture_impl + "\nclass GLDirect3DVertexDeclaration9")

# Fix GLDirect3DDevice9 methods (ensure they are STDMETHODIMP)
text = text.replace("STDMETHOD(CreateRenderTarget)(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) { if (ppSurface) { *ppSurface = new GLDirect3DSurface9(); } return D3D_OK; }",
                    "STDMETHODIMP GLDirect3DDevice9::CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) { if (ppSurface) { *ppSurface = new GLDirect3DSurface9(Width, Height, Format); } return D3D_OK; }")

text = text.replace("STDMETHOD(CreateDepthStencilSurface)(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) { if (ppSurface) { *ppSurface = new GLDirect3DSurface9(); } return D3D_OK; }",
                    "STDMETHODIMP GLDirect3DDevice9::CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) { if (ppSurface) { *ppSurface = new GLDirect3DSurface9(Width, Height, Format); } return D3D_OK; }")

text = text.replace("STDMETHOD(GetRenderTarget)(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) { if (ppRenderTarget) { *ppRenderTarget = new GLDirect3DSurface9(); } return D3D_OK; }",
                    "STDMETHODIMP GLDirect3DDevice9::GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) { if (ppRenderTarget) { *ppRenderTarget = new GLDirect3DSurface9(1024, 768, D3DFMT_A8R8G8B8); } return D3D_OK; }")

text = text.replace("STDMETHOD(GetDepthStencilSurface)(IDirect3DSurface9** ppZStencilSurface) { if (ppZStencilSurface) { *ppZStencilSurface = new GLDirect3DSurface9(); } return D3D_OK; }",
                    "STDMETHODIMP GLDirect3DDevice9::GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) { if (ppZStencilSurface) { *ppZStencilSurface = new GLDirect3DSurface9(1024, 768, D3DFMT_D24S8); } return D3D_OK; }")

# Add implementation for GetDeviceCaps (to return success)
caps_impl = "STDMETHODIMP GLDirect3DDevice9::GetDeviceCaps(D3DCAPS9* pCaps) { if (pCaps) { memset(pCaps, 0, sizeof(D3DCAPS9)); pCaps->DeviceType = D3DDEVTYPE_HAL; pCaps->PixelShaderVersion = D3DPS_VERSION(3, 0); pCaps->VertexShaderVersion = D3DVS_VERSION(3, 0); } return D3D_OK; }"
text = text.replace("STDMETHOD(GetDeviceCaps)(D3DCAPS9* pCaps) { return E_NOTIMPL; }", caps_impl)

with open(path, "w") as f:
    f.write(text)
