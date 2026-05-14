#ifndef GLRENDERER_H_INCLUDED
#define GLRENDERER_H_INCLUDED

#include "Vendor/DirectX/Include/d3d9.h"
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <vector>

class GLDirect3DResource9 : public IDirect3DResource9
{
public:
    GLDirect3DResource9() : m_refCount(1) {}
    virtual ~GLDirect3DResource9() {}

    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) { return E_NOINTERFACE; }
    STDMETHOD_(ULONG,AddRef)() { return ++m_refCount; }
    STDMETHOD_(ULONG,Release)() { if (--m_refCount == 0) { delete this; return 0; } return m_refCount; }

    STDMETHOD(GetDevice)(IDirect3DDevice9** ppDevice) { return E_NOTIMPL; }
    STDMETHOD(SetPrivateData)(REFGUID refguid,CONST void* pData,DWORD SizeOfData,DWORD Flags) { return D3D_OK; }
    STDMETHOD(GetPrivateData)(REFGUID refguid,void* pData,DWORD* pSizeOfData) { return E_NOTIMPL; }
    STDMETHOD(FreePrivateData)(REFGUID refguid) { return D3D_OK; }
    STDMETHOD_(DWORD, SetPriority)(DWORD PriorityNew) { return 0; }
    STDMETHOD_(DWORD, GetPriority)() { return 0; }
    STDMETHOD_(void, PreLoad)() {}
    STDMETHOD_(D3DRESOURCETYPE, GetType)() { return D3DRTYPE_SURFACE; }

protected:
    ULONG m_refCount;
};

class GLDirect3DVertexBuffer9 : public GLDirect3DResource9, public IDirect3DVertexBuffer9
{
public:
    GLDirect3DVertexBuffer9(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool);
    virtual ~GLDirect3DVertexBuffer9();

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
    STDMETHOD_(D3DRESOURCETYPE, GetType)() { return D3DRTYPE_VERTEXBUFFER; }

    STDMETHOD(Lock)(UINT OffsetToLock,UINT SizeToLock,void** ppbData,DWORD Flags);
    STDMETHOD(Unlock)();
    STDMETHOD(GetDesc)(D3DVERTEXBUFFER_DESC *pDesc);

    GLuint GetVBO() const { return m_vbo; }

private:
    UINT m_length;
    void* m_pData;
    GLuint m_vbo;
};

class GLDirect3DIndexBuffer9 : public GLDirect3DResource9, public IDirect3DIndexBuffer9
{
public:
    GLDirect3DIndexBuffer9(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool);
    virtual ~GLDirect3DIndexBuffer9();

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
    STDMETHOD_(D3DRESOURCETYPE, GetType)() { return D3DRTYPE_INDEXBUFFER; }

    STDMETHOD(Lock)(UINT OffsetToLock,UINT SizeToLock,void** ppbData,DWORD Flags);
    STDMETHOD(Unlock)();
    STDMETHOD(GetDesc)(D3DINDEXBUFFER_DESC *pDesc);

    GLuint GetIBO() const { return m_ibo; }
    D3DFORMAT GetFormat() const { return m_format; }

private:
    UINT m_length;
    D3DFORMAT m_format;
    void* m_pData;
    GLuint m_ibo;
};

class GLDirect3DTexture9 : public GLDirect3DResource9, public IDirect3DTexture9
{
public:
    GLDirect3DTexture9(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool);
    virtual ~GLDirect3DTexture9();

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
    STDMETHOD_(D3DRESOURCETYPE, GetType)() { return D3DRTYPE_TEXTURE; }

    STDMETHOD_(DWORD, SetLOD)(DWORD LODNew) { return 0; }
    STDMETHOD_(DWORD, GetLOD)() { return 0; }
    STDMETHOD_(DWORD, GetLevelCount)() { return m_levels; }
    STDMETHOD(SetAutoGenFilterType)(D3DTEXTUREFILTERTYPE FilterType) { return D3D_OK; }
    STDMETHOD_(D3DTEXTUREFILTERTYPE, GetAutoGenFilterType)() { return D3DTEXF_NONE; }
    STDMETHOD_(void, GenerateMipSubLevels)() {}
    STDMETHOD(GetLevelDesc)(UINT Level,D3DSURFACE_DESC *pDesc) { return E_NOTIMPL; }
    STDMETHOD(GetSurfaceLevel)(UINT Level,IDirect3DSurface9** ppSurfaceLevel) { return E_NOTIMPL; }
    STDMETHOD(LockRect)(UINT Level,D3DLOCKED_RECT* pLockedRect,CONST RECT* pRect,DWORD Flags);
    STDMETHOD(UnlockRect)(UINT Level);
    STDMETHOD(AddDirtyRect)(CONST RECT* pDirtyRect) { return D3D_OK; }

    GLuint GetTex() const { return m_tex; }

private:
    GLuint m_tex;
    UINT m_width, m_height;
    D3DFORMAT m_format;
    UINT m_levels;
    void* m_pData;
};

class GLDirect3DVertexDeclaration9 : public IDirect3DVertexDeclaration9
{
public:
    GLDirect3DVertexDeclaration9(CONST D3DVERTEXELEMENT9* pVertexElements);
    virtual ~GLDirect3DVertexDeclaration9() {}

    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) { return E_NOINTERFACE; }
    STDMETHOD_(ULONG,AddRef)() { return ++m_refCount; }
    STDMETHOD_(ULONG,Release)() { if (--m_refCount == 0) { delete this; return 0; } return m_refCount; }

    STDMETHOD(GetDevice)(IDirect3DDevice9** ppDevice) { return E_NOTIMPL; }
    STDMETHOD(GetDeclaration)(D3DVERTEXELEMENT9* pElement,UINT* pNumElements);

    const std::vector<D3DVERTEXELEMENT9>& GetElements() const { return m_elements; }

private:
    ULONG m_refCount;
    std::vector<D3DVERTEXELEMENT9> m_elements;
};

class GLDirect3DSurface9 : public GLDirect3DResource9, public IDirect3DSurface9
{
public:
    GLDirect3DSurface9() : GLDirect3DResource9() {}
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
    STDMETHOD(GetDesc)(D3DSURFACE_DESC *pDesc) { return E_NOTIMPL; }
    STDMETHOD(LockRect)(D3DLOCKED_RECT* pLockedRect,CONST RECT* pRect,DWORD Flags) { return E_NOTIMPL; }
    STDMETHOD(UnlockRect)() { return D3D_OK; }
    STDMETHOD(GetDC)(HDC *phdc) { return E_NOTIMPL; }
    STDMETHOD(ReleaseDC)(HDC hdc) { return D3D_OK; }
};

class GLDirect3DQuery9 : public IDirect3DQuery9
{
public:
    GLDirect3DQuery9() : m_refCount(1) {}
    virtual ~GLDirect3DQuery9() {}

    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) { return E_NOINTERFACE; }
    STDMETHOD_(ULONG,AddRef)() { return ++m_refCount; }
    STDMETHOD_(ULONG,Release)() { if (--m_refCount == 0) { delete this; return 0; } return m_refCount; }

    STDMETHOD(GetDevice)(IDirect3DDevice9** ppDevice) { return E_NOTIMPL; }
    STDMETHOD_(D3DQUERYTYPE, GetType)() { return D3DQUERYTYPE_EVENT; }
    STDMETHOD_(DWORD, GetDataSize)() { return 0; }
    STDMETHOD(Issue)(DWORD dwIssueFlags) { return D3D_OK; }
    STDMETHOD(GetData)(void* pData,DWORD dwSize,DWORD dwGetDataFlags) { return D3D_OK; }

private:
    ULONG m_refCount;
};

class GLDirect3DVertexShader9 : public IDirect3DVertexShader9
{
public:
    GLDirect3DVertexShader9(CONST DWORD* pFunction);
    virtual ~GLDirect3DVertexShader9() {}

    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) { return E_NOINTERFACE; }
    STDMETHOD_(ULONG,AddRef)() { return ++m_refCount; }
    STDMETHOD_(ULONG,Release)() { if (--m_refCount == 0) { delete this; return 0; } return m_refCount; }

    STDMETHOD(GetDevice)(IDirect3DDevice9** ppDevice) { return E_NOTIMPL; }
    STDMETHOD(GetFunction)(void* pData,UINT* pSizeOfData);

private:
    ULONG m_refCount;
    std::vector<DWORD> m_function;
};

class GLDirect3DPixelShader9 : public IDirect3DPixelShader9
{
public:
    GLDirect3DPixelShader9(CONST DWORD* pFunction);
    virtual ~GLDirect3DPixelShader9() {}

    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) { return E_NOINTERFACE; }
    STDMETHOD_(ULONG,AddRef)() { return ++m_refCount; }
    STDMETHOD_(ULONG,Release)() { if (--m_refCount == 0) { delete this; return 0; } return m_refCount; }

    STDMETHOD(GetDevice)(IDirect3DDevice9** ppDevice) { return E_NOTIMPL; }
    STDMETHOD(GetFunction)(void* pData,UINT* pSizeOfData);

private:
    ULONG m_refCount;
    std::vector<DWORD> m_function;
};

class GLDirect3D9 : public IDirect3D9
{
public:
    GLDirect3D9();
    virtual ~GLDirect3D9();

    /*** IUnknown methods ***/
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj);
    STDMETHOD_(ULONG,AddRef)();
    STDMETHOD_(ULONG,Release)();

    /*** IDirect3D9 methods ***/
    STDMETHOD(RegisterSoftwareDevice)(void* pInitializeFunction);
    STDMETHOD_(UINT, GetAdapterCount)();
    STDMETHOD(GetAdapterIdentifier)(UINT Adapter,DWORD Flags,D3DADAPTER_IDENTIFIER9* pIdentifier);
    STDMETHOD_(UINT, GetAdapterModeCount)(UINT Adapter,D3DFORMAT Format);
    STDMETHOD(EnumAdapterModes)(UINT Adapter,D3DFORMAT Format,UINT Mode,D3DDISPLAYMODE* pMode);
    STDMETHOD(GetAdapterDisplayMode)(UINT Adapter,D3DDISPLAYMODE* pMode);
    STDMETHOD(CheckDeviceType)(UINT Adapter,D3DDEVTYPE DevType,D3DFORMAT AdapterFormat,D3DFORMAT BackBufferFormat,BOOL bWindowed);
    STDMETHOD(CheckDeviceFormat)(UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT AdapterFormat,DWORD Usage,D3DRESOURCETYPE RType,D3DFORMAT CheckFormat);
    STDMETHOD(CheckDeviceMultiSampleType)(UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT SurfaceFormat,BOOL Windowed,D3DMULTISAMPLE_TYPE MultiSampleType,DWORD* pQualityLevels);
    STDMETHOD(CheckDepthStencilMatch)(UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT AdapterFormat,D3DFORMAT RenderTargetFormat,D3DFORMAT DepthStencilFormat);
    STDMETHOD(CheckDeviceFormatConversion)(UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT SourceFormat,D3DFORMAT TargetFormat);
    STDMETHOD(GetDeviceCaps)(UINT Adapter,D3DDEVTYPE DeviceType,D3DCAPS9* pCaps);
    STDMETHOD_(HMONITOR, GetAdapterMonitor)(UINT Adapter);
    STDMETHOD(CreateDevice)(UINT Adapter,D3DDEVTYPE DeviceType,HWND hFocusWindow,DWORD BehaviorFlags,D3DPRESENT_PARAMETERS* pPresentationParameters,IDirect3DDevice9** ppReturnedDeviceInterface);

private:
    ULONG m_refCount;
};

class GLDirect3DDevice9 : public IDirect3DDevice9
{
public:
    GLDirect3DDevice9(IDirect3D9* pD3D, HWND hWnd, D3DPRESENT_PARAMETERS* pPresentationParameters);
    virtual ~GLDirect3DDevice9();

    void SetSDLWindow(void* pWindow) { m_sdlWindow = pWindow; }

    /*** IUnknown methods ***/
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj);
    STDMETHOD_(ULONG,AddRef)();
    STDMETHOD_(ULONG,Release)();

    /*** IDirect3DDevice9 methods ***/
    STDMETHOD(TestCooperativeLevel)();
    STDMETHOD_(UINT, GetAvailableTextureMem)();
    STDMETHOD(EvictManagedResources)();
    STDMETHOD(GetDirect3D)(IDirect3D9** ppD3D9);
    STDMETHOD(GetDeviceCaps)(D3DCAPS9* pCaps);
    STDMETHOD(GetDisplayMode)(UINT iSwapChain,D3DDISPLAYMODE* pMode);
    STDMETHOD(GetCreationParameters)(D3DDEVICE_CREATION_PARAMETERS *pParameters);
    STDMETHOD(SetCursorProperties)(UINT XHotSpot,UINT YHotSpot,IDirect3DSurface9* pCursorBitmap);
    STDMETHOD_(void, SetCursorPosition)(int X,int Y,DWORD Flags);
    STDMETHOD_(BOOL, ShowCursor)(BOOL bShow);
    STDMETHOD(CreateAdditionalSwapChain)(D3DPRESENT_PARAMETERS* pPresentationParameters,IDirect3DSwapChain9** pSwapChain);
    STDMETHOD(GetSwapChain)(UINT iSwapChain,IDirect3DSwapChain9** pSwapChain);
    STDMETHOD_(UINT, GetNumberOfSwapChains)();
    STDMETHOD(Reset)(D3DPRESENT_PARAMETERS* pPresentationParameters);
    STDMETHOD(Present)(CONST RECT* pSourceRect,CONST RECT* pDestRect,HWND hDestWindowOverride,CONST RGNDATA* pDirtyRegion);
    STDMETHOD(GetBackBuffer)(UINT iSwapChain,UINT iBackBuffer,D3DBACKBUFFER_TYPE Type,IDirect3DSurface9** ppBackBuffer);
    STDMETHOD(GetRasterStatus)(UINT iSwapChain,D3DRASTER_STATUS* pRasterStatus);
    STDMETHOD(SetDialogBoxMode)(BOOL bEnableDialogs);
    STDMETHOD_(void, SetGammaRamp)(UINT iSwapChain,DWORD Flags,CONST D3DGAMMARAMP* pRamp);
    STDMETHOD_(void, GetGammaRamp)(UINT iSwapChain,D3DGAMMARAMP* pRamp);
    STDMETHOD(CreateTexture)(UINT Width,UINT Height,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DTexture9** ppTexture,HANDLE* pSharedHandle);
    STDMETHOD(CreateVolumeTexture)(UINT Width,UINT Height,UINT Depth,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DVolumeTexture9** ppVolumeTexture,HANDLE* pSharedHandle);
    STDMETHOD(CreateCubeTexture)(UINT EdgeLength,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DCubeTexture9** ppCubeTexture,HANDLE* pSharedHandle);
    STDMETHOD(CreateVertexBuffer)(UINT Length,DWORD Usage,DWORD FVF,D3DPOOL Pool,IDirect3DVertexBuffer9** ppVertexBuffer,HANDLE* pSharedHandle);
    STDMETHOD(CreateIndexBuffer)(UINT Length,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DIndexBuffer9** ppIndexBuffer,HANDLE* pSharedHandle);
    STDMETHOD(CreateRenderTarget)(UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Lockable,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle);
    STDMETHOD(CreateDepthStencilSurface)(UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Discard,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle);
    STDMETHOD(UpdateSurface)(IDirect3DSurface9* pSourceSurface,CONST RECT* pSourceRect,IDirect3DSurface9* pDestinationSurface,CONST POINT* pDestPoint);
    STDMETHOD(UpdateTexture)(IDirect3DBaseTexture9* pSourceTexture,IDirect3DBaseTexture9* pDestinationTexture);
    STDMETHOD(GetRenderTargetData)(IDirect3DSurface9* pRenderTarget,IDirect3DSurface9* pDestSurface);
    STDMETHOD(GetFrontBufferData)(UINT iSwapChain,IDirect3DSurface9* pDestSurface);
    STDMETHOD(StretchRect)(IDirect3DSurface9* pSourceSurface,CONST RECT* pSourceRect,IDirect3DSurface9* pDestSurface,CONST RECT* pDestRect,D3DTEXTUREFILTERTYPE Filter);
    STDMETHOD(ColorFill)(IDirect3DSurface9* pSurface,CONST RECT* pRect,D3DCOLOR color);
    STDMETHOD(CreateOffscreenPlainSurface)(UINT Width,UINT Height,D3DFORMAT Format,D3DPOOL Pool,IDirect3DSurface9** ppSurface,HANDLE* pSharedHandle);
    STDMETHOD(SetRenderTarget)(DWORD RenderTargetIndex,IDirect3DSurface9* pRenderTarget);
    STDMETHOD(GetRenderTarget)(DWORD RenderTargetIndex,IDirect3DSurface9** ppRenderTarget);
    STDMETHOD(SetDepthStencilSurface)(IDirect3DSurface9* pNewZStencil);
    STDMETHOD(GetDepthStencilSurface)(IDirect3DSurface9** ppZStencilSurface);
    STDMETHOD(BeginScene)();
    STDMETHOD(EndScene)();
    STDMETHOD(Clear)(DWORD Count,CONST D3DRECT* pRects,DWORD Flags,D3DCOLOR Color,float Z,DWORD Stencil);
    STDMETHOD(SetTransform)(D3DTRANSFORMSTATETYPE State,CONST D3DMATRIX* pMatrix);
    STDMETHOD(GetTransform)(D3DTRANSFORMSTATETYPE State,D3DMATRIX* pMatrix);
    STDMETHOD(MultiplyTransform)(D3DTRANSFORMSTATETYPE State,CONST D3DMATRIX* pMatrix);
    STDMETHOD(SetViewport)(CONST D3DVIEWPORT9* pViewport);
    STDMETHOD(GetViewport)(D3DVIEWPORT9* pViewport);
    STDMETHOD(SetMaterial)(CONST D3DMATERIAL9* pMaterial);
    STDMETHOD(GetMaterial)(D3DMATERIAL9* pMaterial);
    STDMETHOD(SetLight)(DWORD Index,CONST D3DLIGHT9* pLight);
    STDMETHOD(GetLight)(DWORD Index,D3DLIGHT9* pLight);
    STDMETHOD(LightEnable)(DWORD Index,BOOL Enable);
    STDMETHOD(GetLightEnable)(DWORD Index,BOOL* pEnable);
    STDMETHOD(SetClipPlane)(DWORD Index,CONST float* pPlane);
    STDMETHOD(GetClipPlane)(DWORD Index,float* pPlane);
    STDMETHOD(SetRenderState)(D3DRENDERSTATETYPE State,DWORD Value);
    STDMETHOD(GetRenderState)(D3DRENDERSTATETYPE State,DWORD* pValue);
    STDMETHOD(CreateStateBlock)(D3DSTATEBLOCKTYPE Type,IDirect3DStateBlock9** ppSB);
    STDMETHOD(BeginStateBlock)();
    STDMETHOD(EndStateBlock)(IDirect3DStateBlock9** ppSB);
    STDMETHOD(SetClipStatus)(CONST D3DCLIPSTATUS9* pClipStatus);
    STDMETHOD(GetClipStatus)(D3DCLIPSTATUS9* pClipStatus);
    STDMETHOD(GetTexture)(DWORD Stage,IDirect3DBaseTexture9** ppTexture);
    STDMETHOD(SetTexture)(DWORD Stage,IDirect3DBaseTexture9* pTexture);
    STDMETHOD(GetTextureStageState)(DWORD Stage,D3DTEXTURESTAGESTATETYPE Type,DWORD* pValue);
    STDMETHOD(SetTextureStageState)(DWORD Stage,D3DTEXTURESTAGESTATETYPE Type,DWORD Value);
    STDMETHOD(GetSamplerState)(DWORD Sampler,D3DSAMPLERSTATETYPE Type,DWORD* pValue);
    STDMETHOD(SetSamplerState)(DWORD Sampler,D3DSAMPLERSTATETYPE Type,DWORD Value);
    STDMETHOD(ValidateDevice)(DWORD* pNumPasses);
    STDMETHOD(SetPaletteEntries)(UINT PaletteNumber,CONST PALETTEENTRY* pEntries);
    STDMETHOD(GetPaletteEntries)(UINT PaletteNumber,PALETTEENTRY* pEntries);
    STDMETHOD(SetCurrentTexturePalette)(UINT PaletteNumber);
    STDMETHOD(GetCurrentTexturePalette)(UINT* pPaletteNumber);
    STDMETHOD(SetScissorRect)(CONST RECT* pRect);
    STDMETHOD(GetScissorRect)(RECT* pRect);
    STDMETHOD(SetSoftwareVertexProcessing)(BOOL bSoftware);
    STDMETHOD_(BOOL, GetSoftwareVertexProcessing)();
    STDMETHOD(SetNPatchMode)(float nSegments);
    STDMETHOD_(float, GetNPatchMode)();
    STDMETHOD(DrawPrimitive)(D3DPRIMITIVETYPE PrimitiveType,UINT StartVertex,UINT PrimitiveCount);
    STDMETHOD(DrawIndexedPrimitive)(D3DPRIMITIVETYPE,INT BaseVertexIndex,UINT MinVertexIndex,UINT NumVertices,UINT startIndex,UINT primCount);
    STDMETHOD(DrawPrimitiveUP)(D3DPRIMITIVETYPE PrimitiveType,UINT PrimitiveCount,CONST void* pVertexStreamZeroData,UINT VertexStreamZeroStride);
    STDMETHOD(DrawIndexedPrimitiveUP)(D3DPRIMITIVETYPE PrimitiveType,UINT MinVertexIndex,UINT NumVertices,UINT PrimitiveCount,CONST void* pIndexData,D3DFORMAT IndexDataFormat,CONST void* pVertexStreamZeroData,UINT VertexStreamZeroStride);
    STDMETHOD(ProcessVertices)(UINT SrcStartIndex,UINT DestIndex,UINT VertexCount,IDirect3DVertexBuffer9* pDestBuffer,IDirect3DVertexDeclaration9* pVertexDeclaration,DWORD Flags);
    STDMETHOD(CreateVertexDeclaration)(CONST D3DVERTEXELEMENT9* pVertexElements,IDirect3DVertexDeclaration9** ppDecl);
    STDMETHOD(SetVertexDeclaration)(IDirect3DVertexDeclaration9* pDecl);
    STDMETHOD(GetVertexDeclaration)(IDirect3DVertexDeclaration9** ppDecl);
    STDMETHOD(SetFVF)(DWORD FVF);
    STDMETHOD(GetFVF)(DWORD* pFVF);
    STDMETHOD(CreateVertexShader)(CONST DWORD* pFunction,IDirect3DVertexShader9** ppShader);
    STDMETHOD(SetVertexShader)(IDirect3DVertexShader9* pShader);
    STDMETHOD(GetVertexShader)(IDirect3DVertexShader9** ppShader);
    STDMETHOD(SetVertexShaderConstantF)(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount);
    STDMETHOD(GetVertexShaderConstantF)(UINT StartRegister,float* pConstantData,UINT Vector4fCount);
    STDMETHOD(SetVertexShaderConstantI)(UINT StartRegister,CONST int* pConstantData,UINT Vector4iCount);
    STDMETHOD(GetVertexShaderConstantI)(UINT StartRegister,int* pConstantData,UINT Vector4iCount);
    STDMETHOD(SetVertexShaderConstantB)(UINT StartRegister,CONST BOOL* pConstantData,UINT  BoolCount);
    STDMETHOD(GetVertexShaderConstantB)(UINT StartRegister,BOOL* pConstantData,UINT BoolCount);
    STDMETHOD(CreatePixelShader)(CONST DWORD* pFunction,IDirect3DPixelShader9** ppShader);
    STDMETHOD(SetPixelShader)(IDirect3DPixelShader9* pShader);
    STDMETHOD(GetPixelShader)(IDirect3DPixelShader9** ppShader);
    STDMETHOD(SetPixelShaderConstantF)(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount);
    STDMETHOD(GetPixelShaderConstantF)(UINT StartRegister,float* pConstantData,UINT Vector4fCount);
    STDMETHOD(SetPixelShaderConstantI)(UINT StartRegister,CONST int* pConstantData,UINT Vector4iCount);
    STDMETHOD(GetPixelShaderConstantI)(UINT StartRegister,int* pConstantData,UINT Vector4iCount);
    STDMETHOD(SetPixelShaderConstantB)(UINT StartRegister, CONST BOOL* pConstantData, UINT  BoolCount);
    STDMETHOD(GetPixelShaderConstantB)(UINT StartRegister, BOOL* pConstantData, UINT BoolCount);
    STDMETHOD(DrawRectPatch)(UINT Handle,CONST float* pNumSegs,CONST D3DRECTPATCH_INFO* pPatchInfo);
    STDMETHOD(DrawTriPatch)(UINT Handle,CONST float* pNumSegs,CONST D3DTRIPATCH_INFO* pPatchInfo);
    STDMETHOD(DeletePatch)(UINT Handle);
    STDMETHOD(CreateQuery)(D3DQUERYTYPE Type,IDirect3DQuery9** ppQuery);
    STDMETHOD(SetStreamSource)(UINT StreamNumber,IDirect3DVertexBuffer9* pStreamData,UINT OffsetInBytes,UINT Stride);
    STDMETHOD(GetStreamSource)(UINT StreamNumber,IDirect3DVertexBuffer9** ppStreamData,UINT* OffsetInBytes,UINT* pStride);
    STDMETHOD(SetStreamSourceFreq)(UINT StreamNumber,UINT Setting);
    STDMETHOD(GetStreamSourceFreq)(UINT StreamNumber,UINT* pSetting);
    STDMETHOD(SetIndices)(IDirect3DIndexBuffer9* pIndexData);
    STDMETHOD(GetIndices)(IDirect3DIndexBuffer9** ppIndexData);

private:
    void UpdateShaderProgram();
    GLuint CompileShader(GLenum type, const char* source);

    ULONG m_refCount;
    IDirect3D9* m_pD3D;
    HWND m_hWnd;
    void* m_sdlWindow;
    D3DPRESENT_PARAMETERS m_presentParams;

    struct StreamSource {
        IDirect3DVertexBuffer9* pStreamData;
        UINT OffsetInBytes;
        UINT Stride;
        StreamSource() : pStreamData(NULL), OffsetInBytes(0), Stride(0) {}
    };

    StreamSource m_streams[16];
    IDirect3DIndexBuffer9* m_pIndexData;
    IDirect3DVertexDeclaration9* m_pVertexDecl;
    DWORD m_fvf;

    IDirect3DVertexShader9* m_pVertexShader;
    IDirect3DPixelShader9* m_pPixelShader;

    float m_vsConstF[256][4];
    float m_psConstF[256][4];

    GLuint m_shaderProg;
    bool m_shaderDirty;
};

#endif
