#ifndef GLRENDERER_H_INCLUDED
#define GLRENDERER_H_INCLUDED

#if !defined(_WIN32)

#include "Vendor/DirectX/Include/d3d9.h"
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <vector>

class GLDirect3DResource9 : public IUnknown {
public:
    GLDirect3DResource9() : m_refCount(1) {}
    virtual ~GLDirect3DResource9() {}
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) { return E_NOINTERFACE; }
    STDMETHOD_(ULONG,AddRef)() { return ++m_refCount; }
    STDMETHOD_(ULONG,Release)() { if (--m_refCount == 0) { delete this; return 0; } return m_refCount; }
    STDMETHOD(GetDevice)(IDirect3DDevice9** ppDevice) { return D3D_OK; }
    STDMETHOD(SetPrivateData)(REFGUID refguid,CONST void* pData,DWORD SizeOfData,DWORD Flags) { return D3D_OK; }
    STDMETHOD(GetPrivateData)(REFGUID refguid,void* pData,DWORD* pSizeOfData) { return D3D_OK; }
    STDMETHOD(FreePrivateData)(REFGUID refguid) { return D3D_OK; }
    STDMETHOD_(DWORD, SetPriority)(DWORD PriorityNew) { return 0; }
    STDMETHOD_(DWORD, GetPriority)() { return 0; }
    STDMETHOD_(void, PreLoad)() {}
    STDMETHOD_(D3DRESOURCETYPE, GetType)() { return D3DRTYPE_SURFACE; }
protected:
    LONG m_refCount;
};

class GLDirect3DVertexBuffer9 : public GLDirect3DResource9, public IDirect3DVertexBuffer9 {
public:
    GLDirect3DVertexBuffer9(UINT len, DWORD u, DWORD fvf, D3DPOOL p);
    virtual ~GLDirect3DVertexBuffer9();
    STDMETHOD(QueryInterface)(REFIID r, void** p) { return GLDirect3DResource9::QueryInterface(r, p); }
    STDMETHOD_(ULONG,AddRef)() { return GLDirect3DResource9::AddRef(); }
    STDMETHOD_(ULONG,Release)() { return GLDirect3DResource9::Release(); }
    STDMETHOD(GetDevice)(IDirect3DDevice9** p) { return D3D_OK; }
    STDMETHOD(SetPrivateData)(REFGUID r,CONST void* d,DWORD s,DWORD f) { return D3D_OK; }
    STDMETHOD(GetPrivateData)(REFGUID r,void* d,DWORD* s) { return D3D_OK; }
    STDMETHOD(FreePrivateData)(REFGUID r) { return D3D_OK; }
    STDMETHOD_(DWORD, SetPriority)(DWORD p) { return 0; }
    STDMETHOD_(DWORD, GetPriority)() { return 0; }
    STDMETHOD_(void, PreLoad)() {}
    STDMETHOD_(D3DRESOURCETYPE, GetType)() { return D3DRTYPE_VERTEXBUFFER; }
    STDMETHOD(Lock)(UINT o,UINT s,void** p,DWORD f);
    STDMETHOD(Unlock)();
    STDMETHOD(GetDesc)(D3DVERTEXBUFFER_DESC *p);
    GLuint GetVBO() const { return m_vbo; }
    void* m_pData;
private:
    UINT m_length;
    GLuint m_vbo;
};

class GLDirect3DIndexBuffer9 : public GLDirect3DResource9, public IDirect3DIndexBuffer9 {
public:
    GLDirect3DIndexBuffer9(UINT len, DWORD u, D3DFORMAT f, D3DPOOL p);
    virtual ~GLDirect3DIndexBuffer9();
    STDMETHOD(QueryInterface)(REFIID r, void** p) { return GLDirect3DResource9::QueryInterface(r, p); }
    STDMETHOD_(ULONG,AddRef)() { return GLDirect3DResource9::AddRef(); }
    STDMETHOD_(ULONG,Release)() { return GLDirect3DResource9::Release(); }
    STDMETHOD(GetDevice)(IDirect3DDevice9** p) { return D3D_OK; }
    STDMETHOD(SetPrivateData)(REFGUID r,CONST void* d,DWORD s,DWORD f) { return D3D_OK; }
    STDMETHOD(GetPrivateData)(REFGUID r,void* d,DWORD* s) { return D3D_OK; }
    STDMETHOD(FreePrivateData)(REFGUID r) { return D3D_OK; }
    STDMETHOD_(DWORD, SetPriority)(DWORD p) { return 0; }
    STDMETHOD_(DWORD, GetPriority)() { return 0; }
    STDMETHOD_(void, PreLoad)() {}
    STDMETHOD_(D3DRESOURCETYPE, GetType)() { return D3DRTYPE_INDEXBUFFER; }
    STDMETHOD(Lock)(UINT o,UINT s,void** p,DWORD f);
    STDMETHOD(Unlock)();
    STDMETHOD(GetDesc)(D3DINDEXBUFFER_DESC *p);
    GLuint GetIBO() const { return m_ibo; }
    D3DFORMAT GetFormat() const { return m_format; }
    void* m_pData;
private:
    UINT m_length;
    D3DFORMAT m_format;
    GLuint m_ibo;
};

class GLDirect3DTexture9 : public GLDirect3DResource9, public IDirect3DTexture9 {
public:
    GLDirect3DTexture9(UINT w, UINT h, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p);
    virtual ~GLDirect3DTexture9();
    STDMETHOD(QueryInterface)(REFIID r, void** p) { return GLDirect3DResource9::QueryInterface(r, p); }
    STDMETHOD_(ULONG,AddRef)() { return GLDirect3DResource9::AddRef(); }
    STDMETHOD_(ULONG,Release)() { return GLDirect3DResource9::Release(); }
    STDMETHOD(GetDevice)(IDirect3DDevice9** p) { return D3D_OK; }
    STDMETHOD(SetPrivateData)(REFGUID r,CONST void* d,DWORD s,DWORD f) { return D3D_OK; }
    STDMETHOD(GetPrivateData)(REFGUID r,void* d,DWORD* s) { return D3D_OK; }
    STDMETHOD(FreePrivateData)(REFGUID r) { return D3D_OK; }
    STDMETHOD_(DWORD, SetPriority)(DWORD p) { return 0; }
    STDMETHOD_(DWORD, GetPriority)() { return 0; }
    STDMETHOD_(void, PreLoad)() {}
    STDMETHOD_(D3DRESOURCETYPE, GetType)() { return D3DRTYPE_TEXTURE; }
    STDMETHOD_(DWORD, GetLevelCount)() { return m_levels; }
    STDMETHOD_(DWORD, SetLOD)(DWORD l) { return 0; }
    STDMETHOD_(DWORD, GetLOD)() { return 0; }
    STDMETHOD(LockRect)(UINT l,D3DLOCKED_RECT* p,CONST RECT* r,DWORD f);
    STDMETHOD(UnlockRect)(UINT l);
    STDMETHOD(GetLevelDesc)(UINT l,D3DSURFACE_DESC *p);
    STDMETHOD(GetSurfaceLevel)(UINT l, IDirect3DSurface9** ppS);
    STDMETHOD(SetAutoGenFilterType)(D3DTEXTUREFILTERTYPE t) { return D3D_OK; }
    STDMETHOD_(D3DTEXTUREFILTERTYPE, GetAutoGenFilterType)() { return D3DTEXF_NONE; }
    STDMETHOD_(void, GenerateMipSubLevels)() {}
    STDMETHOD(AddDirtyRect)(CONST RECT* r) { return D3D_OK; }
    GLuint GetTex() const { return m_tex; }
    GLuint GetFBO();
    UINT GetWidth() const { return m_width; }
    UINT GetHeight() const { return m_height; }
private:
    GLuint m_tex, m_fbo;
    UINT m_width, m_height, m_levels;
    D3DFORMAT m_format;
    void* m_pData;
};

class GLDirect3DSurface9 : public GLDirect3DResource9, public IDirect3DSurface9 {
public:
    GLDirect3DSurface9(GLDirect3DTexture9* p, UINT l);
    STDMETHOD(QueryInterface)(REFIID r, void** p) { return GLDirect3DResource9::QueryInterface(r, p); }
    STDMETHOD_(ULONG,AddRef)() { return GLDirect3DResource9::AddRef(); }
    STDMETHOD_(ULONG,Release)() { return GLDirect3DResource9::Release(); }
    STDMETHOD(GetDevice)(IDirect3DDevice9** p) { return D3D_OK; }
    STDMETHOD(SetPrivateData)(REFGUID r,CONST void* d,DWORD s,DWORD f) { return D3D_OK; }
    STDMETHOD(GetPrivateData)(REFGUID r,void* d,DWORD* s) { return D3D_OK; }
    STDMETHOD(FreePrivateData)(REFGUID r) { return D3D_OK; }
    STDMETHOD_(DWORD, SetPriority)(DWORD p) { return 0; }
    STDMETHOD_(DWORD, GetPriority)() { return 0; }
    STDMETHOD_(void, PreLoad)() {}
    STDMETHOD_(D3DRESOURCETYPE, GetType)() { return D3DRTYPE_SURFACE; }
    STDMETHOD(GetDesc)(D3DSURFACE_DESC *p);
    STDMETHOD(LockRect)(D3DLOCKED_RECT* p,CONST RECT* r,DWORD f);
    STDMETHOD(UnlockRect)();
    STDMETHOD(GetContainer)(REFIID r,void** p) { return E_NOTIMPL; }
    STDMETHOD(GetDC)(HDC *p) { return E_NOTIMPL; }
    STDMETHOD(ReleaseDC)(HDC h) { return E_NOTIMPL; }
    GLDirect3DTexture9* GetParent() const { return m_pParent; }
private:
    GLDirect3DTexture9* m_pParent;
    UINT m_level;
};

class GLDirect3DVertexDeclaration9 : public IDirect3DVertexDeclaration9 {
public:
    GLDirect3DVertexDeclaration9(CONST D3DVERTEXELEMENT9* e);
    STDMETHOD(QueryInterface)(REFIID r, void** p) { return E_NOINTERFACE; }
    STDMETHOD_(ULONG,AddRef)() { return ++m_refCount; }
    STDMETHOD_(ULONG,Release)() { if(--m_refCount==0){delete this; return 0;} return m_refCount; }
    STDMETHOD(GetDevice)(IDirect3DDevice9** p) { return D3D_OK; }
    STDMETHOD(GetDeclaration)(D3DVERTEXELEMENT9* e, UINT* n);
    const std::vector<D3DVERTEXELEMENT9>& GetElements() const { return m_elements; }
private:
    LONG m_refCount;
    std::vector<D3DVERTEXELEMENT9> m_elements;
};

class GLDirect3DVertexShader9 : public IDirect3DVertexShader9 {
public:
    GLDirect3DVertexShader9(CONST DWORD* f);
    STDMETHOD(QueryInterface)(REFIID r, void** p) { return E_NOINTERFACE; }
    STDMETHOD_(ULONG,AddRef)() { return ++m_refCount; }
    STDMETHOD_(ULONG,Release)() { if(--m_refCount==0){delete this; return 0;} return m_refCount; }
    STDMETHOD(GetDevice)(IDirect3DDevice9** p) { return D3D_OK; }
    STDMETHOD(GetFunction)(void* d, UINT* s);
private:
    LONG m_refCount;
    std::vector<DWORD> m_function;
};

class GLDirect3DPixelShader9 : public IDirect3DPixelShader9 {
public:
    GLDirect3DPixelShader9(CONST DWORD* f);
    STDMETHOD(QueryInterface)(REFIID r, void** p) { return E_NOINTERFACE; }
    STDMETHOD_(ULONG,AddRef)() { return ++m_refCount; }
    STDMETHOD_(ULONG,Release)() { if(--m_refCount==0){delete this; return 0;} return m_refCount; }
    STDMETHOD(GetDevice)(IDirect3DDevice9** p) { return D3D_OK; }
    STDMETHOD(GetFunction)(void* d, UINT* s);
private:
    LONG m_refCount;
    std::vector<DWORD> m_function;
};

class GLDirect3DQuery9 : public IDirect3DQuery9 {
public:
    GLDirect3DQuery9() : m_refCount(1) {}
    STDMETHOD(QueryInterface)(REFIID r, void** p) { return E_NOINTERFACE; }
    STDMETHOD_(ULONG,AddRef)() { return ++m_refCount; }
    STDMETHOD_(ULONG,Release)() { if(--m_refCount==0){delete this; return 0;} return m_refCount; }
    STDMETHOD(GetDevice)(IDirect3DDevice9** p) { return D3D_OK; }
    STDMETHOD_(D3DQUERYTYPE, GetType)() { return D3DQUERYTYPE_EVENT; }
    STDMETHOD_(DWORD, GetDataSize)() { return 0; }
    STDMETHOD(Issue)(DWORD f) { return D3D_OK; }
    STDMETHOD(GetData)(void* d,DWORD s,DWORD f) { return D3D_OK; }
private:
    LONG m_refCount;
};

class GLDirect3DDevice9 : public IDirect3DDevice9 {
public:
    GLDirect3DDevice9(IDirect3D9* pD3D, HWND hWnd, D3DPRESENT_PARAMETERS* pPP);
    virtual ~GLDirect3DDevice9();
    STDMETHOD(QueryInterface)(REFIID r, void** p);
    STDMETHOD_(ULONG,AddRef)(); STDMETHOD_(ULONG,Release)();
    STDMETHOD(TestCooperativeLevel)();
    STDMETHOD_(UINT, GetAvailableTextureMem)();
    STDMETHOD(EvictManagedResources)();
    STDMETHOD(GetDirect3D)(IDirect3D9** p);
    STDMETHOD(GetDeviceCaps)(D3DCAPS9* p);
    STDMETHOD(GetDisplayMode)(UINT i,D3DDISPLAYMODE* p);
    STDMETHOD(GetCreationParameters)(D3DDEVICE_CREATION_PARAMETERS *p);
    STDMETHOD(SetCursorProperties)(UINT x,UINT y,IDirect3DSurface9* p);
    STDMETHOD_(void, SetCursorPosition)(int x,int y,DWORD f);
    STDMETHOD_(BOOL, ShowCursor)(BOOL b);
    STDMETHOD(CreateAdditionalSwapChain)(D3DPRESENT_PARAMETERS* p,IDirect3DSwapChain9** s);
    STDMETHOD(GetSwapChain)(UINT i,IDirect3DSwapChain9** s);
    STDMETHOD_(UINT, GetNumberOfSwapChains)();
    STDMETHOD(Reset)(D3DPRESENT_PARAMETERS* p);
    STDMETHOD(Present)(CONST RECT* s,CONST RECT* d,HWND h,CONST RGNDATA* r);
    STDMETHOD(GetBackBuffer)(UINT i,UINT b,D3DBACKBUFFER_TYPE t,IDirect3DSurface9** p);
    STDMETHOD(GetRasterStatus)(UINT i,D3DRASTER_STATUS* p);
    STDMETHOD(SetDialogBoxMode)(BOOL b);
    STDMETHOD_(void, SetGammaRamp)(UINT i,DWORD f,CONST D3DGAMMARAMP* r);
    STDMETHOD_(void, GetGammaRamp)(UINT i,D3DGAMMARAMP* r);
    STDMETHOD(CreateTexture)(UINT w,UINT h,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DTexture9** t,HANDLE* s);
    STDMETHOD(CreateVolumeTexture)(UINT w,UINT h,UINT d,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DVolumeTexture9** t,HANDLE* s);
    STDMETHOD(CreateCubeTexture)(UINT e,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DCubeTexture9** t,HANDLE* s);
    STDMETHOD(CreateVertexBuffer)(UINT l,DWORD u,DWORD f,D3DPOOL p,IDirect3DVertexBuffer9** v,HANDLE* s);
    STDMETHOD(CreateIndexBuffer)(UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DIndexBuffer9** i,HANDLE* s);
    STDMETHOD(CreateRenderTarget)(UINT w,UINT h,D3DFORMAT f,D3DMULTISAMPLE_TYPE m,DWORD q,BOOL l,IDirect3DSurface9** s,HANDLE* sh);
    STDMETHOD(CreateDepthStencilSurface)(UINT w,UINT h,D3DFORMAT f,D3DMULTISAMPLE_TYPE m,DWORD q,BOOL d,IDirect3DSurface9** s,HANDLE* sh);
    STDMETHOD(UpdateSurface)(IDirect3DSurface9* s,CONST RECT* sr,IDirect3DSurface9* d,CONST POINT* dp);
    STDMETHOD(UpdateTexture)(IDirect3DBaseTexture9* s,IDirect3DBaseTexture9* d);
    STDMETHOD(GetRenderTargetData)(IDirect3DSurface9* r,IDirect3DSurface9* d);
    STDMETHOD(GetFrontBufferData)(UINT i,IDirect3DSurface9* d);
    STDMETHOD(StretchRect)(IDirect3DSurface9* s,CONST RECT* sr,IDirect3DSurface9* d,CONST RECT* dr,D3DTEXTUREFILTERTYPE f);
    STDMETHOD(ColorFill)(IDirect3DSurface9* s,CONST RECT* r,D3DCOLOR c);
    STDMETHOD(CreateOffscreenPlainSurface)(UINT w,UINT h,D3DFORMAT f,D3DPOOL p,IDirect3DSurface9** s,HANDLE* sh);
    STDMETHOD(SetRenderTarget)(DWORD i,IDirect3DSurface9* r);
    STDMETHOD(GetRenderTarget)(DWORD i,IDirect3DSurface9** r);
    STDMETHOD(SetDepthStencilSurface)(IDirect3DSurface9* z);
    STDMETHOD(GetDepthStencilSurface)(IDirect3DSurface9** z);
    STDMETHOD(BeginScene)();
    STDMETHOD(EndScene)();
    STDMETHOD(Clear)(DWORD c,CONST D3DRECT* r,DWORD f,D3DCOLOR col,float z,DWORD s);
    STDMETHOD(SetTransform)(D3DTRANSFORMSTATETYPE s,CONST D3DMATRIX* m);
    STDMETHOD(GetTransform)(D3DTRANSFORMSTATETYPE s,D3DMATRIX* m);
    STDMETHOD(MultiplyTransform)(D3DTRANSFORMSTATETYPE s,CONST D3DMATRIX* m);
    STDMETHOD(SetViewport)(CONST D3DVIEWPORT9* v);
    STDMETHOD(GetViewport)(D3DVIEWPORT9* v);
    STDMETHOD(SetMaterial)(CONST D3DMATERIAL9* m);
    STDMETHOD(GetMaterial)(D3DMATERIAL9* m);
    STDMETHOD(SetLight)(DWORD i,CONST D3DLIGHT9* l);
    STDMETHOD(GetLight)(DWORD i,D3DLIGHT9* l);
    STDMETHOD(LightEnable)(DWORD i,BOOL e);
    STDMETHOD(GetLightEnable)(DWORD i,BOOL* e);
    STDMETHOD(SetClipPlane)(DWORD i,CONST float* p);
    STDMETHOD(GetClipPlane)(DWORD i,float* p);
    STDMETHOD(SetRenderState)(D3DRENDERSTATETYPE s,DWORD v);
    STDMETHOD(GetRenderState)(D3DRENDERSTATETYPE s,DWORD* v);
    STDMETHOD(CreateStateBlock)(D3DSTATEBLOCKTYPE t,IDirect3DStateBlock9** s);
    STDMETHOD(BeginStateBlock)();
    STDMETHOD(EndStateBlock)(IDirect3DStateBlock9** s);
    STDMETHOD(SetClipStatus)(CONST D3DCLIPSTATUS9* c);
    STDMETHOD(GetClipStatus)(D3DCLIPSTATUS9* c);
    STDMETHOD(GetTexture)(DWORD s,IDirect3DBaseTexture9** t);
    STDMETHOD(SetTexture)(DWORD s,IDirect3DBaseTexture9* t);
    STDMETHOD(GetTextureStageState)(DWORD s,D3DTEXTURESTAGESTATETYPE t,DWORD* v);
    STDMETHOD(SetTextureStageState)(DWORD s,D3DTEXTURESTAGESTATETYPE t,DWORD v);
    STDMETHOD(GetSamplerState)(DWORD s,D3DSAMPLERSTATETYPE t,DWORD* v);
    STDMETHOD(SetSamplerState)(DWORD s,D3DSAMPLERSTATETYPE t,DWORD v);
    STDMETHOD(ValidateDevice)(DWORD* n);
    STDMETHOD(SetPaletteEntries)(UINT n,CONST PALETTEENTRY* p);
    STDMETHOD(GetPaletteEntries)(UINT n,PALETTEENTRY* p);
    STDMETHOD(SetCurrentTexturePalette)(UINT n);
    STDMETHOD(GetCurrentTexturePalette)(UINT *n);
    STDMETHOD(SetScissorRect)(CONST RECT* p);
    STDMETHOD(GetScissorRect)(RECT* r);
    STDMETHOD(SetSoftwareVertexProcessing)(BOOL s);
    STDMETHOD_(BOOL, GetSoftwareVertexProcessing)();
    STDMETHOD(SetNPatchMode)(float n);
    STDMETHOD_(float, GetNPatchMode)();
    STDMETHOD(DrawPrimitive)(D3DPRIMITIVETYPE t,UINT s,UINT c);
    STDMETHOD(DrawIndexedPrimitive)(D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
    STDMETHOD(DrawPrimitiveUP)(D3DPRIMITIVETYPE,UINT,CONST void*,UINT);
    STDMETHOD(DrawIndexedPrimitiveUP)(D3DPRIMITIVETYPE,UINT,UINT,UINT,CONST void*,D3DFORMAT,CONST void*,UINT);
    STDMETHOD(ProcessVertices)(UINT,UINT,UINT,IDirect3DVertexBuffer9*,IDirect3DVertexDeclaration9*,DWORD);
    STDMETHOD(CreateVertexDeclaration)(CONST D3DVERTEXELEMENT9*,IDirect3DVertexDeclaration9**);
    STDMETHOD(SetVertexDeclaration)(IDirect3DVertexDeclaration9*);
    STDMETHOD(GetVertexDeclaration)(IDirect3DVertexDeclaration9**);
    STDMETHOD(SetFVF)(DWORD);
    STDMETHOD(GetFVF)(DWORD*);
    STDMETHOD(CreateVertexShader)(CONST DWORD*,IDirect3DVertexShader9**);
    STDMETHOD(SetVertexShader)(IDirect3DVertexShader9*);
    STDMETHOD(GetVertexShader)(IDirect3DVertexShader9**);
    STDMETHOD(SetVertexShaderConstantF)(UINT,CONST float*,UINT);
    STDMETHOD(GetVertexShaderConstantF)(UINT,float*,UINT);
    STDMETHOD(SetVertexShaderConstantI)(UINT,CONST int*,UINT);
    STDMETHOD(GetVertexShaderConstantI)(UINT,int*,UINT);
    STDMETHOD(SetVertexShaderConstantB)(UINT,CONST BOOL*,UINT);
    STDMETHOD(GetVertexShaderConstantB)(UINT,BOOL*,UINT);
    STDMETHOD(SetStreamSource)(UINT,IDirect3DVertexBuffer9*,UINT,UINT);
    STDMETHOD(GetStreamSource)(UINT,IDirect3DVertexBuffer9**,UINT*,UINT*);
    STDMETHOD(SetStreamSourceFreq)(UINT,UINT);
    STDMETHOD(GetStreamSourceFreq)(UINT,UINT*);
    STDMETHOD(SetIndices)(IDirect3DIndexBuffer9*);
    STDMETHOD(GetIndices)(IDirect3DIndexBuffer9**);
    STDMETHOD(CreatePixelShader)(CONST DWORD*,IDirect3DPixelShader9**);
    STDMETHOD(SetPixelShader)(IDirect3DPixelShader9*);
    STDMETHOD(GetPixelShader)(IDirect3DPixelShader9**);
    STDMETHOD(SetPixelShaderConstantF)(UINT,CONST float*,UINT);
    STDMETHOD(GetPixelShaderConstantF)(UINT,float*,UINT);
    STDMETHOD(SetPixelShaderConstantI)(UINT,CONST int*,UINT);
    STDMETHOD(GetPixelShaderConstantI)(UINT,int*,UINT);
    STDMETHOD(SetPixelShaderConstantB)(UINT,CONST BOOL*,UINT);
    STDMETHOD(GetPixelShaderConstantB)(UINT,BOOL*,UINT);
    STDMETHOD(DrawRectPatch)(UINT,CONST float*,CONST D3DRECTPATCH_INFO*);
    STDMETHOD(DrawTriPatch)(UINT,CONST float*,CONST D3DTRIPATCH_INFO*);
    STDMETHOD(DeletePatch)(UINT);
    STDMETHOD(CreateQuery)(D3DQUERYTYPE,IDirect3DQuery9**);
    void SetSDLWindow(void* w);
private:
    bool IsRHW(const void* d);
    void UpdateShaderProgram(bool isRHW);
    GLuint CompileShader(GLenum type, const char* source);
    void ApplyAttributes(const void* pUP, UINT ups, UINT startV);
    LONG m_refCount;
    IDirect3D9* m_pD3D;
    HWND m_hWnd;
    D3DPRESENT_PARAMETERS m_presentParams;
    IDirect3DIndexBuffer9* m_pIndexData;
    IDirect3DVertexDeclaration9* m_pVertexDecl;
    DWORD m_fvf;
    IDirect3DVertexShader9* m_pVertexShader;
    IDirect3DPixelShader9* m_pPixelShader;
    IDirect3DBaseTexture9* m_textures[16];
    float m_vsConstF[256][4], m_psConstF[256][4];
    struct Stream { IDirect3DVertexBuffer9* pStreamData; UINT OffsetInBytes; UINT Stride; } m_streams[16];
    GLuint m_shaderProg;
    bool m_shaderDirty;
};

class GLDirect3D9 : public IDirect3D9 {
public:
    GLDirect3D9(); virtual ~GLDirect3D9();
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj);
    STDMETHOD_(ULONG,AddRef)(); STDMETHOD_(ULONG,Release)();
    STDMETHOD(RegisterSoftwareDevice)(void* pInit);
    STDMETHOD_(UINT, GetAdapterCount)();
    STDMETHOD(GetAdapterIdentifier)(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pId);
    STDMETHOD_(UINT, GetAdapterModeCount)(UINT Adapter, D3DFORMAT Format);
    STDMETHOD(EnumAdapterModes)(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode);
    STDMETHOD(GetAdapterDisplayMode)(UINT Adapter, D3DDISPLAYMODE* pMode);
    STDMETHOD(CheckDeviceType)(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdFmt, D3DFORMAT BbFmt, BOOL bWin);
    STDMETHOD(CheckDeviceFormat)(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdFmt, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat);
    STDMETHOD(CheckDeviceMultiSampleType)(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT SurfFmt, BOOL Win, D3DMULTISAMPLE_TYPE MSType, DWORD* pQual);
    STDMETHOD(CheckDepthStencilMatch)(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdFmt, D3DFORMAT RtFmt, D3DFORMAT DsFmt);
    STDMETHOD(CheckDeviceFormatConversion)(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT SrcFmt, D3DFORMAT TgtFmt);
    STDMETHOD(GetDeviceCaps)(UINT Adapter, D3DDEVTYPE DevType, D3DCAPS9* pCaps);
    STDMETHOD_(HMONITOR, GetAdapterMonitor)(UINT Adapter);
    STDMETHOD(CreateDevice)(UINT Adapter, D3DDEVTYPE DevType, HWND hWnd, DWORD Flags, D3DPRESENT_PARAMETERS* pPP, IDirect3DDevice9** ppDev);
private:
    LONG m_refCount;
};

#endif // !defined(_WIN32)

#endif
