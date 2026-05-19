import os
import re

path = "pw/branches/r1117/Src/Render/GLRenderer.h"
with open(path, "r") as f:
    text = f.read()

# Update GLDirect3DSurface9 declaration to include parent and level
surface_decl = r"""class GLDirect3DSurface9 : public GLDirect3DResource9, public IDirect3DSurface9
{
public:
    GLDirect3DSurface9(GLDirect3DTexture9* pParent = NULL, UINT level = 0);
    virtual ~GLDirect3DSurface9() {}

    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) { return GLDirect3DResource9::QueryInterface(riid, ppvObj); }
    STDMETHOD_(ULONG,AddRef)() { return GLDirect3DResource9::AddRef(); }
    STDMETHOD_(ULONG,Release)() { return GLDirect3DResource9::Release(); }

    STDMETHOD(GetDevice)(IDirect3DDevice9** ppDevice) { return GLDirect3DResource9::GetDevice(ppDevice); }
    STDMETHOD(SetPrivateData)(REFGUID refguid,CONST void* pData,DWORD SizeOfData,DWORD Flags) { return GLDirect3DResource9::SetPrivateData(refguid, pData, SizeOfData, Flags); }
    STDMETHOD(GetPrivateData)(REFGUID refguid,void* pData,DWORD* pSizeOfData) { return GLDirect3DResource9::GetPrivateData(refguid, pData, pSizeOfData); }
    STDMETHOD(FreePrivateData)(REFGUID refguid) { return GLDirect3DResource9::FreePrivateData(refguid); }
    STDMETHOD_(DWORD, SetPriority)(DWORD PriorityNew) { return GLDirect3DResource9::SetPriority(PriorityNew); }
    STDMETHOD_(DWORD, GetPriority)() { return GLDirect3DResource9::GetPriority(); }
    STDMETHOD_(void, PreLoad)() { GLDirect3DResource9::PreLoad(); }
    STDMETHOD_(D3DRESOURCETYPE, GetType)() { return D3DRTYPE_SURFACE; }

    STDMETHOD(GetContainer)(REFIID riid,void** ppContainer) { return E_NOTIMPL; }
    STDMETHOD(GetDesc)(D3DSURFACE_DESC *pDesc);
    STDMETHOD(LockRect)(D3DLOCKED_RECT* pLockedRect,CONST RECT* pRect,DWORD Flags);
    STDMETHOD(UnlockRect)();
    STDMETHOD(GetDC)(HDC *phdc) { return E_NOTIMPL; }
    STDMETHOD(ReleaseDC)(HDC hdc) { return D3D_OK; }

private:
    GLDirect3DTexture9* m_pParent;
    UINT m_level;
};"""

# Replace old surface decl
text = re.sub(r'class GLDirect3DSurface9 : public GLDirect3DResource9, public IDirect3DSurface9[\s\S]*?};', surface_decl, text)

with open(path, "w") as f:
    f.write(text)
