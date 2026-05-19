import re

path = "pw/branches/r1117/Src/Render/GLRenderer.cpp"
with open(path, "r") as f:
    text = f.read()

# GLDirect3DSurface9 definition
surface_impl = """
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
    STDMETHOD(LockRect)(D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags) { 
        if (m_pParent) return m_pParent->LockRect(m_level, pLockedRect, pRect, Flags);
        return E_NOTIMPL;
    }
    STDMETHOD(UnlockRect)() { 
        if (m_pParent) return m_pParent->UnlockRect(m_level);
        return E_NOTIMPL;
    }
    STDMETHOD(GetDC)(HDC* phdc) { return E_NOTIMPL; }
    STDMETHOD(ReleaseDC)(HDC hdc) { return E_NOTIMPL; }
};
"""

# Insert Surface definition if not present
if "class GLDirect3DSurface9" not in text:
    text = text.replace("class GLDirect3DTexture9", surface_impl + "\nclass GLDirect3DTexture9")

# Update GLDirect3DTexture9::GetSurfaceLevel
texture_update = """
    STDMETHOD(GetSurfaceLevel)(UINT Level, IDirect3DSurface9** ppSurfaceLevel) {
        if (ppSurfaceLevel) { *ppSurfaceLevel = new GLDirect3DSurface9(m_width, m_height, m_format, this, Level); }
        return D3D_OK;
    }
"""
if "STDMETHOD(GetSurfaceLevel)" not in text:
    text = re.sub(r'(STDMETHOD\(UnlockRect\)\(UINT Level\) \{[\s\S]*?return D3D_OK;\n\s*\})', r'\1' + texture_update, text)

# Implementation of missing device methods
text = text.replace("STDMETHOD(GetRenderTarget)(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) { if (ppRenderTarget) { *ppRenderTarget = new GLDirect3DSurface9(); } return D3D_OK; }",
                    "STDMETHOD(GetRenderTarget)(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) { if (ppRenderTarget) { *ppRenderTarget = new GLDirect3DSurface9(1024, 768, D3DFMT_A8R8G8B8); } return D3D_OK; }")

text = text.replace("STDMETHOD(GetDepthStencilSurface)(IDirect3DSurface9** ppZStencilSurface) { if (ppZStencilSurface) { *ppZStencilSurface = new GLDirect3DSurface9(); } return D3D_OK; }",
                    "STDMETHOD(GetDepthStencilSurface)(IDirect3DSurface9** ppZStencilSurface) { if (ppZStencilSurface) { *ppZStencilSurface = new GLDirect3DSurface9(1024, 768, D3DFMT_D24S8); } return D3D_OK; }")

# Add implementation for SetSamplerState (to avoid E_NOTIMPL if called)
sampler_state_impl = "STDMETHOD(SetSamplerState)(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) { return D3D_OK; }"
text = text.replace("STDMETHOD(SetSamplerState)(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) { return E_NOTIMPL; }", sampler_state_impl)

# Add implementation for GetDeviceCaps (to return success)
caps_impl = "STDMETHOD(GetDeviceCaps)(D3DCAPS9* pCaps) { if (pCaps) { memset(pCaps, 0, sizeof(D3DCAPS9)); pCaps->DeviceType = D3DDEVTYPE_HAL; pCaps->PixelShaderVersion = D3DPS_VERSION(3, 0); pCaps->VertexShaderVersion = D3DVS_VERSION(3, 0); } return D3D_OK; }"
text = text.replace("STDMETHOD(GetDeviceCaps)(D3DCAPS9* pCaps) { return E_NOTIMPL; }", caps_impl)

with open(path, "w") as f:
    f.write(text)
