#include "stdafx.h"
#include "GLRenderer.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <string.h>
#include <algorithm>
#include <map>

// D3DX includes
#include "Vendor/DirectX/Include/d3dx9.h"

static int g_drawCalls = 0;

static uint32_t HashBytecode(const DWORD* pFunction, UINT size) {
    uint32_t h = 0x811c9dc5;
    for (UINT i = 0; i < size; ++i) {
        h ^= pFunction[i];
        h *= 0x01000193;
    }
    return h;
}

extern "C" IDirect3D9 * WINAPI Direct3DCreate9(UINT SDKVersion) {
    return new GLDirect3D9();
}

extern "C" {
  const char* WINAPI DXGetErrorDescriptionA(HRESULT hr) { return ""; }
  const char* WINAPI DXGetErrorStringA(HRESULT hr) { return ""; }
  
  HRESULT WINAPI D3DXCompileShader(LPCSTR pSrcData, UINT SrcDataLen, CONST D3DXMACRO* pDefines, LPD3DXINCLUDE pInclude, LPCSTR pFunctionName, LPCSTR pProfile, DWORD Flags, LPD3DXBUFFER* ppShader, LPD3DXBUFFER* ppErrorMsgs, LPD3DXCONSTANTTABLE* ppConstantTable) { return D3D_OK; }
  
  HRESULT WINAPI D3DXSaveSurfaceToFileA(LPCSTR pDestFile, D3DXIMAGE_FILEFORMAT DestFormat, LPDIRECT3DSURFACE9 pSrcSurface, CONST PALETTEENTRY* pSrcPalette, CONST RECT* pSrcRect) { return D3D_OK; }
  
  HRESULT WINAPI D3DXSaveSurfaceToFileInMemory(LPD3DXBUFFER* ppDestBuf, D3DXIMAGE_FILEFORMAT DestFormat, LPDIRECT3DSURFACE9 pSrcSurface, CONST PALETTEENTRY* pSrcPalette, CONST RECT* pSrcRect) { return D3D_OK; }
  
  HRESULT WINAPI D3DXLoadSurfaceFromSurface(LPDIRECT3DSURFACE9 pDestSurface, CONST PALETTEENTRY* pDestPalette, CONST RECT* pDestRect, LPDIRECT3DSURFACE9 pSrcSurface, CONST PALETTEENTRY* pSrcPalette, CONST RECT* pSrcRect, DWORD Filter, D3DCOLOR ColorKey) { return D3D_OK; }
  
  D3DXVECTOR3* WINAPI D3DXVec3Normalize(D3DXVECTOR3* pOut, CONST D3DXVECTOR3* pV) { if (pOut && pV) *pOut = *pV; return pOut; }
  
  FLOAT* WINAPI D3DXSHEvalDirection(FLOAT* pOut, UINT Order, CONST D3DXVECTOR3* pDir) { return pOut; }
  
  void WINAPI D3DPERF_SetMarker(D3DCOLOR col, LPCWSTR wszName) {}
}

GLDirect3D9::GLDirect3D9() : m_refCount(1) {
    fprintf(stderr, "GLDirect3D9 created\n");
    fflush(stderr);
}

GLDirect3D9::~GLDirect3D9() {}

STDMETHODIMP GLDirect3D9::QueryInterface(REFIID riid, void** ppvObj) { return E_NOINTERFACE; }
STDMETHODIMP_(ULONG) GLDirect3D9::AddRef() { return ++m_refCount; }
STDMETHODIMP_(ULONG) GLDirect3D9::Release() { if (--m_refCount == 0) { delete this; return 0; } return m_refCount; }

STDMETHODIMP GLDirect3D9::RegisterSoftwareDevice(void* pInitializeFunction) { return E_NOTIMPL; }
STDMETHODIMP_(UINT) GLDirect3D9::GetAdapterCount() { return 1; }
STDMETHODIMP GLDirect3D9::GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pIdentifier) {
    if (pIdentifier) {
        memset(pIdentifier, 0, sizeof(D3DADAPTER_IDENTIFIER9));
        strncpy(pIdentifier->Description, "OpenGL Renderer", sizeof(pIdentifier->Description));
        strncpy(pIdentifier->DeviceName, "GL0", sizeof(pIdentifier->DeviceName));
    }
    return D3D_OK;
}
STDMETHODIMP_(UINT) GLDirect3D9::GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) { return 1; }
STDMETHODIMP GLDirect3D9::EnumAdapterModes(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode) {
    if (pMode) {
        pMode->Width = 1024;
        pMode->Height = 768;
        pMode->RefreshRate = 60;
        pMode->Format = D3DFMT_X8R8G8B8;
    }
    return D3D_OK;
}
STDMETHODIMP GLDirect3D9::GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE* pMode) {
    return EnumAdapterModes(Adapter, D3DFMT_X8R8G8B8, 0, pMode);
}
STDMETHODIMP GLDirect3D9::CheckDeviceType(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed) { return D3D_OK; }
STDMETHODIMP GLDirect3D9::CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) { return D3D_OK; }
STDMETHODIMP GLDirect3D9::CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD* pQualityLevels) { return D3D_OK; }
STDMETHODIMP GLDirect3D9::CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) { return D3D_OK; }
STDMETHODIMP GLDirect3D9::CheckDeviceFormatConversion(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat) { return D3D_OK; }
STDMETHODIMP GLDirect3D9::GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps) {
    if (pCaps) {
        memset(pCaps, 0, sizeof(D3DCAPS9));
        pCaps->DeviceType = D3DDEVTYPE_HAL;
        pCaps->Caps = D3DCAPS_READ_SCANLINE;
        pCaps->PixelShaderVersion = D3DPS_VERSION(3, 0);
        pCaps->VertexShaderVersion = D3DVS_VERSION(3, 0);
        pCaps->MaxVertexShaderConst = 256;
    }
    return D3D_OK;
}
STDMETHODIMP_(HMONITOR) GLDirect3D9::GetAdapterMonitor(UINT Adapter) { return (HMONITOR)1; }
STDMETHODIMP GLDirect3D9::CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface) {
    if (ppReturnedDeviceInterface) {
        *ppReturnedDeviceInterface = new GLDirect3DDevice9(this, hFocusWindow, pPresentationParameters);
    }
    return D3D_OK;
}

// GLDirect3DDevice9 Implementation

GLDirect3DDevice9::GLDirect3DDevice9(IDirect3D9* pD3D, HWND hWnd, D3DPRESENT_PARAMETERS* pPresentationParameters) : m_refCount(1), m_pD3D(pD3D), m_hWnd(hWnd), m_sdlWindow(NULL), m_pIndexData(NULL), m_pVertexDecl(NULL), m_fvf(0), m_pVertexShader(NULL), m_pPixelShader(NULL), m_shaderProg(0), m_shaderDirty(true) {
    if (pPresentationParameters) m_presentParams = *pPresentationParameters;
    memset(m_vsConstF, 0, sizeof(m_vsConstF));
    memset(m_psConstF, 0, sizeof(m_psConstF));
    fprintf(stderr, "GLDirect3DDevice9 created for HWND %p\n", hWnd);
    fflush(stderr);
}

GLDirect3DDevice9::~GLDirect3DDevice9() {
    if (m_shaderProg) glDeleteProgram(m_shaderProg);
}

STDMETHODIMP GLDirect3DDevice9::QueryInterface(REFIID riid, void** ppvObj) { return E_NOINTERFACE; }
STDMETHODIMP_(ULONG) GLDirect3DDevice9::AddRef() { return ++m_refCount; }
STDMETHODIMP_(ULONG) GLDirect3DDevice9::Release() { if (--m_refCount == 0) { delete this; return 0; } return m_refCount; }

STDMETHODIMP GLDirect3DDevice9::TestCooperativeLevel() { return D3D_OK; }
STDMETHODIMP_(UINT) GLDirect3DDevice9::GetAvailableTextureMem() { return 1024 * 1024 * 1024; }
STDMETHODIMP GLDirect3DDevice9::EvictManagedResources() { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetDirect3D(IDirect3D9** ppD3D9) {
    if (ppD3D9) {
        *ppD3D9 = m_pD3D;
        m_pD3D->AddRef();
    }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::GetDeviceCaps(D3DCAPS9* pCaps) {
    return m_pD3D->GetDeviceCaps(0, D3DDEVTYPE_HAL, pCaps);
}
STDMETHODIMP GLDirect3DDevice9::GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE* pMode) {
    return m_pD3D->GetAdapterDisplayMode(0, pMode);
}
STDMETHODIMP GLDirect3DDevice9::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap) { return D3D_OK; }
STDMETHODIMP_(void) GLDirect3DDevice9::SetCursorPosition(int X, int Y, DWORD Flags) {}
STDMETHODIMP_(BOOL) GLDirect3DDevice9::ShowCursor(BOOL bShow) { return TRUE; }
STDMETHODIMP GLDirect3DDevice9::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::GetSwapChain(UINT iSwapChain, IDirect3DSwapChain9** pSwapChain) { return E_NOTIMPL; }
STDMETHODIMP_(UINT) GLDirect3DDevice9::GetNumberOfSwapChains() { return 1; }
STDMETHODIMP GLDirect3DDevice9::Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::Present(CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion) {
    if (m_sdlWindow) {
        SDL_GL_SwapWindow((SDL_Window*)m_sdlWindow);
    }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::GetBackBuffer(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) { 
    if (ppBackBuffer) {
        *ppBackBuffer = new GLDirect3DSurface9();
    }
    return D3D_OK; 
}
STDMETHODIMP GLDirect3DDevice9::GetRasterStatus(UINT iSwapChain, D3DRASTER_STATUS* pRasterStatus) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetDialogBoxMode(BOOL bEnableDialogs) { return D3D_OK; }
STDMETHODIMP_(void) GLDirect3DDevice9::SetGammaRamp(UINT iSwapChain, DWORD Flags, CONST D3DGAMMARAMP* pRamp) {}
STDMETHODIMP_(void) GLDirect3DDevice9::GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP* pRamp) {}
STDMETHODIMP GLDirect3DDevice9::CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle) {
    fprintf(stderr, "CreateTexture: %dx%d, levels=%d, usage=%08X, format=%d, pool=%d\n", Width, Height, Levels, Usage, (int)Format, (int)Pool);
    fflush(stderr);
    if (ppTexture) {
        *ppTexture = new GLDirect3DTexture9(Width, Height, Levels, Usage, Format, Pool);
    }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle) {
    if (ppVertexBuffer) {
        *ppVertexBuffer = new GLDirect3DVertexBuffer9(Length, Usage, FVF, Pool);
    }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle) {
    if (ppIndexBuffer) {
        *ppIndexBuffer = new GLDirect3DIndexBuffer9(Length, Usage, Format, Pool);
    }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) {
    if (ppSurface) {
        *ppSurface = new GLDirect3DSurface9();
    }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) {
    if (ppSurface) {
        *ppSurface = new GLDirect3DSurface9();
    }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::UpdateSurface(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestinationSurface, CONST POINT* pDestPoint) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::UpdateTexture(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::StretchRect(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestSurface, CONST RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::ColorFill(IDirect3DSurface9* pSurface, CONST RECT* pRect, D3DCOLOR color) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) {
    if (ppRenderTarget) {
        *ppRenderTarget = new GLDirect3DSurface9();
    }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) {
    if (ppZStencilSurface) {
        *ppZStencilSurface = new GLDirect3DSurface9();
    }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::BeginScene() {
    g_drawCalls = 0;
    fprintf(stderr, "GLDirect3DDevice9::BeginScene\n");
    fflush(stderr);
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::EndScene() {
    fprintf(stderr, "GLDirect3DDevice9::EndScene (Total draws: %d)\n", g_drawCalls);
    fflush(stderr);
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::Clear(DWORD Count, CONST D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) {
    fprintf(stderr, "GLDirect3DDevice9::Clear (Color: %08X)\n", Color);
    fflush(stderr);
    // Force blue clear to verify
    glClearColor(0.2f, 0.3f, 0.8f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::SetTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix) { 
    if (State == D3DTS_PROJECTION) {
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf((const float*)pMatrix);
    } else if (State == D3DTS_VIEW || State == D3DTS_WORLD) {
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf((const float*)pMatrix);
    }
    return D3D_OK; 
}
STDMETHODIMP GLDirect3DDevice9::GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::MultiplyTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::SetViewport(CONST D3DVIEWPORT9* pViewport) { 
    if (pViewport) glViewport(pViewport->X, pViewport->Y, pViewport->Width, pViewport->Height);
    return D3D_OK; 
}
STDMETHODIMP GLDirect3DDevice9::GetViewport(D3DVIEWPORT9* pViewport) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetMaterial(CONST D3DMATERIAL9* pMaterial) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetMaterial(D3DMATERIAL9* pMaterial) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetLight(DWORD Index, CONST D3DLIGHT9* pLight) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetLight(DWORD Index, D3DLIGHT9* pLight) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::LightEnable(DWORD Index, BOOL Enable) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetLightEnable(DWORD Index, BOOL* pEnable) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetClipPlane(DWORD Index, CONST float* pPlane) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetClipPlane(DWORD Index, float* pPlane) { return E_NOTIMPL; }

STDMETHODIMP GLDirect3DDevice9::SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) {
    switch (State) {
        case D3DRS_ZENABLE:
            if (Value) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
            break;
        case D3DRS_ZWRITEENABLE:
            glDepthMask(Value ? GL_TRUE : GL_FALSE);
            break;
        case D3DRS_ALPHABLENDENABLE:
            if (Value) glEnable(GL_BLEND); else glDisable(GL_BLEND);
            break;
        case D3DRS_CULLMODE:
            if (Value == D3DCULL_NONE) glDisable(GL_CULL_FACE);
            else {
                glEnable(GL_CULL_FACE);
                glCullFace(Value == D3DCULL_CW ? GL_FRONT : GL_BACK);
            }
            break;
        default: break;
    }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::CreateStateBlock(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9** ppSB) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::BeginStateBlock() { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::EndStateBlock(IDirect3DStateBlock9** ppSB) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetClipStatus(CONST D3DCLIPSTATUS9* pClipStatus) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetClipStatus(D3DCLIPSTATUS9* pClipStatus) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::GetTexture(DWORD Stage, IDirect3DBaseTexture9** ppTexture) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetTexture(DWORD Stage, IDirect3DBaseTexture9* pTexture) {
    glActiveTexture(GL_TEXTURE0 + Stage);
    if (pTexture) {
        GLDirect3DTexture9* pTex = (GLDirect3DTexture9*)pTexture;
        glBindTexture(GL_TEXTURE_2D, pTex->GetTex());
        glEnable(GL_TEXTURE_2D);
    } else {
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD* pValue) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::ValidateDevice(DWORD* pNumPasses) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::SetPaletteEntries(UINT PaletteNumber, CONST PALETTEENTRY* pEntries) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetPaletteEntries(UINT PaletteNumber, PALETTEENTRY* pEntries) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetCurrentTexturePalette(UINT PaletteNumber) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetCurrentTexturePalette(UINT* pPaletteNumber) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetScissorRect(CONST RECT* pRect) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetScissorRect(RECT* pRect) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetSoftwareVertexProcessing(BOOL bSoftware) { return D3D_OK; }
STDMETHODIMP_(BOOL) GLDirect3DDevice9::GetSoftwareVertexProcessing() { return TRUE; }
STDMETHODIMP GLDirect3DDevice9::SetNPatchMode(float nSegments) { return D3D_OK; }
STDMETHODIMP_(float) GLDirect3DDevice9::GetNPatchMode() { return 0.0f; }

static GLenum GetGLPrimitiveType(D3DPRIMITIVETYPE PrimitiveType) {
    switch (PrimitiveType) {
        case D3DPT_POINTLIST:     return GL_POINTS;
        case D3DPT_LINELIST:      return GL_LINES;
        case D3DPT_LINESTRIP:     return GL_LINE_STRIP;
        case D3DPT_TRIANGLELIST:  return GL_TRIANGLES;
        case D3DPT_TRIANGLESTRIP: return GL_TRIANGLE_STRIP;
        case D3DPT_TRIANGLEFAN:   return GL_TRIANGLE_FAN;
        default: return GL_TRIANGLES;
    }
}

static GLsizei GetGLVertexCount(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount) {
    switch (PrimitiveType) {
        case D3DPT_POINTLIST:     return PrimitiveCount;
        case D3DPT_LINELIST:      return PrimitiveCount * 2;
        case D3DPT_LINESTRIP:     return PrimitiveCount + 1;
        case D3DPT_TRIANGLELIST:  return PrimitiveCount * 3;
        case D3DPT_TRIANGLESTRIP: return PrimitiveCount + 2;
        case D3DPT_TRIANGLEFAN:   return PrimitiveCount + 2;
        default: return 0;
    }
}

STDMETHODIMP GLDirect3DDevice9::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) {
    if (g_drawCalls < 10) {
        fprintf(stderr, "DrawPrimitive #%d: type=%d, count=%d\n", g_drawCalls, (int)PrimitiveType, PrimitiveCount);
        fflush(stderr);
    }
    g_drawCalls++;

    GLenum mode = GetGLPrimitiveType(PrimitiveType);
    GLsizei count = GetGLVertexCount(PrimitiveType, PrimitiveCount);
    
    if (m_pVertexDecl) {
        const std::vector<D3DVERTEXELEMENT9>& elements = ((GLDirect3DVertexDeclaration9*)m_pVertexDecl)->GetElements();
        for (size_t i = 0; i < elements.size(); ++i) {
            const D3DVERTEXELEMENT9& el = elements[i];
            if (m_streams[el.Stream].pStreamData) {
                GLDirect3DVertexBuffer9* pVB = (GLDirect3DVertexBuffer9*)m_streams[el.Stream].pStreamData;
                glBindBuffer(GL_ARRAY_BUFFER, pVB->GetVBO());
                
                int size = 4;
                GLenum type = GL_FLOAT;
                GLboolean normalized = GL_FALSE;
                
                switch (el.Type) {
                    case D3DDECLTYPE_FLOAT1: size = 1; break;
                    case D3DDECLTYPE_FLOAT2: size = 2; break;
                    case D3DDECLTYPE_FLOAT3: size = 3; break;
                    case D3DDECLTYPE_FLOAT4: size = 4; break;
                    case D3DDECLTYPE_D3DCOLOR: size = 4; type = GL_UNSIGNED_BYTE; normalized = GL_TRUE; break;
                    default: size = 4; break;
                }

                if (el.Usage == D3DDECLUSAGE_POSITION || el.Usage == D3DDECLUSAGE_POSITIONT) {
                    glEnableVertexAttribArray(0);
                    glVertexAttribPointer(0, size, type, normalized, m_streams[el.Stream].Stride, (void*)(uintptr_t)(el.Offset + StartVertex * m_streams[el.Stream].Stride));
                }
                else if (el.Usage == D3DDECLUSAGE_COLOR) {
                    glEnableVertexAttribArray(1);
                    glVertexAttribPointer(1, size, type, normalized, m_streams[el.Stream].Stride, (void*)(uintptr_t)(el.Offset + StartVertex * m_streams[el.Stream].Stride));
                }
                else if (el.Usage == D3DDECLUSAGE_TEXCOORD) {
                    glEnableVertexAttribArray(2 + el.UsageIndex);
                    glVertexAttribPointer(2 + el.UsageIndex, size, type, normalized, m_streams[el.Stream].Stride, (void*)(uintptr_t)(el.Offset + StartVertex * m_streams[el.Stream].Stride));
                }
            }
        }
    } else if (m_streams[0].pStreamData) {
        GLDirect3DVertexBuffer9* pVB = (GLDirect3DVertexBuffer9*)m_streams[0].pStreamData;
        glBindBuffer(GL_ARRAY_BUFFER, pVB->GetVBO());
    }

    glDrawArrays(mode, StartVertex, count);
    return D3D_OK;
}

void GLDirect3DDevice9::UpdateShaderProgram() {
    if (m_shaderDirty) {
        const char* vsSource = 
            "#version 120\n"
            "attribute vec3 position;\n"
            "attribute vec4 color;\n"
            "attribute vec2 texcoord0;\n"
            "varying vec4 vColor;\n"
            "varying vec2 vTexCoord;\n"
            "void main() {\n"
            "    vColor = color.bgra;\n"
            "    vTexCoord = texcoord0;\n"
            "    gl_Position = vec4(position.x * (2.0 / 1024.0) - 1.0, 1.0 - position.y * (2.0 / 768.0), 0.5, 1.0);\n"
            "}\n";
        const char* psSource =
            "#version 120\n"
            "varying vec4 vColor;\n"
            "varying vec2 vTexCoord;\n"
            "uniform sampler2D tex0;\n"
            "void main() {\n"
            "    gl_FragColor = vColor;\n"
            "}\n";
        
        if (m_shaderProg) glDeleteProgram(m_shaderProg);
        m_shaderProg = glCreateProgram();
        GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSource);
        GLuint ps = CompileShader(GL_FRAGMENT_SHADER, psSource);
        glAttachShader(m_shaderProg, vs);
        glAttachShader(m_shaderProg, ps);
        glBindAttribLocation(m_shaderProg, 0, "position");
        glBindAttribLocation(m_shaderProg, 1, "color");
        glBindAttribLocation(m_shaderProg, 2, "texcoord0");
        glLinkProgram(m_shaderProg);
        glDeleteShader(vs);
        glDeleteShader(ps);
        m_shaderDirty = false;
    }
    glUseProgram(m_shaderProg);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
}

GLuint GLDirect3DDevice9::CompileShader(GLenum type, const char* source) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, NULL);
    glCompileShader(s);
    GLint status;
    glGetShaderiv(s, GL_COMPILE_STATUS, &status);
    if (!status) {
        char info[512];
        glGetShaderInfoLog(s, 512, NULL, info);
        fprintf(stderr, "Shader compile error: %s\n", info);
    }
    return s;
}

STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT StartVertex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) {
    if (g_drawCalls < 10) {
        fprintf(stderr, "DrawIndexedPrimitive #%d: type=%d, count=%d\n", g_drawCalls, (int)PrimitiveType, primCount);
        fflush(stderr);
    }
    g_drawCalls++;

    UpdateShaderProgram();

    GLenum mode = GetGLPrimitiveType(PrimitiveType);
    GLsizei count = GetGLVertexCount(PrimitiveType, primCount);

    if (m_pIndexData) {
        GLDirect3DIndexBuffer9* pIB = (GLDirect3DIndexBuffer9*)m_pIndexData;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pIB->GetIBO());
    }

    if (m_pVertexDecl) {
        const std::vector<D3DVERTEXELEMENT9>& elements = ((GLDirect3DVertexDeclaration9*)m_pVertexDecl)->GetElements();
        for (size_t i = 0; i < elements.size(); ++i) {
            const D3DVERTEXELEMENT9& el = elements[i];
            if (m_streams[el.Stream].pStreamData) {
                GLDirect3DVertexBuffer9* pVB = (GLDirect3DVertexBuffer9*)m_streams[el.Stream].pStreamData;
                glBindBuffer(GL_ARRAY_BUFFER, pVB->GetVBO());
                
                int size = 4;
                GLenum type = GL_FLOAT;
                GLboolean normalized = GL_FALSE;
                
                switch (el.Type) {
                    case D3DDECLTYPE_FLOAT1: size = 1; break;
                    case D3DDECLTYPE_FLOAT2: size = 2; break;
                    case D3DDECLTYPE_FLOAT3: size = 3; break;
                    case D3DDECLTYPE_FLOAT4: size = 4; break;
                    case D3DDECLTYPE_D3DCOLOR: size = 4; type = GL_UNSIGNED_BYTE; normalized = GL_TRUE; break;
                    default: size = 4; break;
                }

                if (el.Usage == D3DDECLUSAGE_POSITION || el.Usage == D3DDECLUSAGE_POSITIONT) {
                    glEnableVertexAttribArray(0);
                    glVertexAttribPointer(0, size, type, normalized, m_streams[el.Stream].Stride, (void*)(uintptr_t)(el.Offset + StartVertex * m_streams[el.Stream].Stride));
                }
                else if (el.Usage == D3DDECLUSAGE_COLOR) {
                    glEnableVertexAttribArray(1);
                    glVertexAttribPointer(1, size, type, normalized, m_streams[el.Stream].Stride, (void*)(uintptr_t)(el.Offset + StartVertex * m_streams[el.Stream].Stride));
                }
                else if (el.Usage == D3DDECLUSAGE_TEXCOORD) {
                    glEnableVertexAttribArray(2 + el.UsageIndex);
                    glVertexAttribPointer(2 + el.UsageIndex, size, type, normalized, m_streams[el.Stream].Stride, (void*)(uintptr_t)(el.Offset + StartVertex * m_streams[el.Stream].Stride));
                }
            }
        }
    }

    GLenum indexType = GL_UNSIGNED_SHORT;
    int indexSize = 2;
    if (m_pIndexData) {
        if (((GLDirect3DIndexBuffer9*)m_pIndexData)->GetFormat() == D3DFMT_INDEX32) {
            indexType = GL_UNSIGNED_INT;
            indexSize = 4;
        }
    }

    glDrawElements(mode, count, indexType, (void*)(uintptr_t)(startIndex * indexSize));
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) {
    GLenum mode = GetGLPrimitiveType(PrimitiveType);
    GLsizei count = GetGLVertexCount(PrimitiveType, PrimitiveCount);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDrawArrays(mode, 0, count);
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) {
    GLenum mode = GetGLPrimitiveType(PrimitiveType);
    GLsizei count = GetGLVertexCount(PrimitiveType, PrimitiveCount);
    GLenum indexType = (IndexDataFormat == D3DFMT_INDEX32) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDrawElements(mode, count, indexType, pIndexData);
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDeclaration, DWORD Flags) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateVertexDeclaration(CONST D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl) {
    if (ppDecl) {
        *ppDecl = new GLDirect3DVertexDeclaration9(pVertexElements);
    }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl) {
    m_pVertexDecl = pDecl;
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::GetVertexDeclaration(IDirect3DVertexDeclaration9** ppDecl) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetFVF(DWORD FVF) {
    m_fvf = FVF;
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::GetFVF(DWORD* pFVF) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::CreateVertexShader(CONST DWORD* pFunction, IDirect3DVertexShader9** ppShader) {
    if (ppShader) {
        *ppShader = new GLDirect3DVertexShader9(pFunction);
    }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::SetVertexShader(IDirect3DVertexShader9* pShader) {
    m_pVertexShader = pShader;
    fprintf(stderr, "SetVertexShader: %p\n", pShader);
    fflush(stderr);
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::GetVertexShader(IDirect3DVertexShader9** ppShader) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetVertexShaderConstantF(UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount) { 
    if (StartRegister + Vector4fCount <= 256) {
        memcpy(&m_vsConstF[StartRegister], pConstantData, Vector4fCount * 16);
    }
    return D3D_OK; 
}
STDMETHODIMP GLDirect3DDevice9::GetVertexShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetVertexShaderConstantI(UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetVertexShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetVertexShaderConstantB(UINT StartRegister, CONST BOOL* pConstantData, UINT  BoolCount) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetVertexShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::CreatePixelShader(CONST DWORD* pFunction, IDirect3DPixelShader9** ppShader) {
    if (ppShader) {
        *ppShader = new GLDirect3DPixelShader9(pFunction);
    }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::SetPixelShader(IDirect3DPixelShader9* pShader) {
    m_pPixelShader = pShader;
    fprintf(stderr, "SetPixelShader: %p\n", pShader);
    fflush(stderr);
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::GetPixelShader(IDirect3DPixelShader9** ppShader) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetPixelShaderConstantF(UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount) { 
    if (StartRegister + Vector4fCount <= 256) {
        memcpy(&m_psConstF[StartRegister], pConstantData, Vector4fCount * 16);
    }
    return D3D_OK; 
}
STDMETHODIMP GLDirect3DDevice9::GetPixelShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetPixelShaderConstantI(UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetPixelShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetPixelShaderConstantB(UINT StartRegister, CONST BOOL* pConstantData, UINT  BoolCount) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetPixelShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::DrawRectPatch(UINT Handle, CONST float* pNumSegs, CONST D3DRECTPATCH_INFO* pPatchInfo) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::DrawTriPatch(UINT Handle, CONST float* pNumSegs, CONST D3DTRIPATCH_INFO* pPatchInfo) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::DeletePatch(UINT Handle) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery) { 
    if (ppQuery) {
        *ppQuery = new GLDirect3DQuery9();
    }
    return D3D_OK; 
}
STDMETHODIMP GLDirect3DDevice9::SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) {
    if (StreamNumber < 16) {
        m_streams[StreamNumber].pStreamData = pStreamData;
        m_streams[StreamNumber].OffsetInBytes = OffsetInBytes;
        m_streams[StreamNumber].Stride = Stride;
    }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* OffsetInBytes, UINT* pStride) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetStreamSourceFreq(UINT StreamNumber, UINT Setting) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetStreamSourceFreq(UINT StreamNumber, UINT* pSetting) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetIndices(IDirect3DIndexBuffer9* pIndexData) {
    m_pIndexData = pIndexData;
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::GetIndices(IDirect3DIndexBuffer9** ppIndexData) { return E_NOTIMPL; }

// Resource implementations

GLDirect3DVertexBuffer9::GLDirect3DVertexBuffer9(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool) : m_length(Length), m_pData(NULL), m_vbo(0) {
    m_pData = malloc(Length);
    glGenBuffers(1, &m_vbo);
}
GLDirect3DVertexBuffer9::~GLDirect3DVertexBuffer9() {
    if (m_pData) free(m_pData);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
}
STDMETHODIMP GLDirect3DVertexBuffer9::Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) {
    if (ppbData) *ppbData = (char*)m_pData + OffsetToLock;
    return D3D_OK;
}
STDMETHODIMP GLDirect3DVertexBuffer9::Unlock() {
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, m_length, m_pData, GL_DYNAMIC_DRAW);
    return D3D_OK;
}
STDMETHODIMP GLDirect3DVertexBuffer9::GetDesc(D3DVERTEXBUFFER_DESC *pDesc) { return E_NOTIMPL; }

GLDirect3DIndexBuffer9::GLDirect3DIndexBuffer9(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool) : m_length(Length), m_format(Format), m_pData(NULL), m_ibo(0) {
    m_pData = malloc(Length);
    glGenBuffers(1, &m_ibo);
}
GLDirect3DIndexBuffer9::~GLDirect3DIndexBuffer9() {
    if (m_pData) free(m_pData);
    if (m_ibo) glDeleteBuffers(1, &m_ibo);
}
STDMETHODIMP GLDirect3DIndexBuffer9::Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) {
    if (ppbData) *ppbData = (char*)m_pData + OffsetToLock;
    return D3D_OK;
}
STDMETHODIMP GLDirect3DIndexBuffer9::Unlock() {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_length, m_pData, GL_DYNAMIC_DRAW);
    return D3D_OK;
}
STDMETHODIMP GLDirect3DIndexBuffer9::GetDesc(D3DINDEXBUFFER_DESC *pDesc) { return E_NOTIMPL; }

GLDirect3DTexture9::GLDirect3DTexture9(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool) : m_tex(0), m_width(Width), m_height(Height), m_format(Format), m_levels(Levels), m_pData(NULL) {
    m_refCount = 1;
    m_pData = malloc(Width * Height * 4); // Dummy allocation
    glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}
GLDirect3DTexture9::~GLDirect3DTexture9() {
    if (m_pData) free(m_pData);
    if (m_tex) glDeleteTextures(1, &m_tex);
}
STDMETHODIMP GLDirect3DTexture9::LockRect(UINT Level, D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags) {
    if (pLockedRect) {
        pLockedRect->Pitch = m_width * 4;
        pLockedRect->pBits = m_pData;
    }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DTexture9::UnlockRect(UINT Level) {
    static int unlockCalls = 0;
    if (unlockCalls < 50) {
        fprintf(stderr, "Texture UnlockRect: %p, level=%d, format=%08X, size=%dx%d\n", this, Level, m_format, m_width, m_height);
        fflush(stderr);
    }
    unlockCalls++;
    glBindTexture(GL_TEXTURE_2D, m_tex);
    if (m_format == 827611204) { // DXT1
        glCompressedTexImage2D(GL_TEXTURE_2D, Level, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, m_width, m_height, 0, std::max(1u, m_width/4) * std::max(1u, m_height/4) * 8, m_pData);
    } else if (m_format == 894720068) { // DXT5
        glCompressedTexImage2D(GL_TEXTURE_2D, Level, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, m_width, m_height, 0, std::max(1u, m_width/4) * std::max(1u, m_height/4) * 16, m_pData);
    } else {
        glTexImage2D(GL_TEXTURE_2D, Level, GL_RGBA, m_width, m_height, 0, GL_BGRA, GL_UNSIGNED_BYTE, m_pData);
    }
    return D3D_OK;
}

GLDirect3DVertexDeclaration9::GLDirect3DVertexDeclaration9(CONST D3DVERTEXELEMENT9* pVertexElements) : m_refCount(1) {
    while (pVertexElements->Stream != 0xFF) {
        m_elements.push_back(*pVertexElements);
        pVertexElements++;
    }
}
STDMETHODIMP GLDirect3DVertexDeclaration9::GetDeclaration(D3DVERTEXELEMENT9* pElement, UINT* pNumElements) {
    if (pNumElements) *pNumElements = (UINT)m_elements.size();
    if (pElement) {
        for (size_t i = 0; i < m_elements.size(); ++i) pElement[i] = m_elements[i];
    }
    return D3D_OK;
}

GLDirect3DVertexShader9::GLDirect3DVertexShader9(CONST DWORD* pFunction) : m_refCount(1) {
    if (pFunction) {
        UINT size = 0;
        while (pFunction[size] != 0x0000FFFF) size++;
        size++;
        uint32_t hash = HashBytecode(pFunction, size);
        fprintf(stderr, "CreateVertexShader: size=%d, hash=%08X, magic=%08X\n", size, hash, pFunction[0]);
        for (UINT i = 0; i < std::min(size, 16u); ++i) {
            fprintf(stderr, "  token[%d] = %08X\n", i, pFunction[i]);
        }
        fflush(stderr);
        m_function.assign(pFunction, pFunction + size);
    }
}
STDMETHODIMP GLDirect3DVertexShader9::GetFunction(void* pData, UINT* pSizeOfData) {
    if (pSizeOfData) *pSizeOfData = (UINT)(m_function.size() * sizeof(DWORD));
    if (pData) memcpy(pData, &m_function[0], m_function.size() * sizeof(DWORD));
    return D3D_OK;
}

GLDirect3DPixelShader9::GLDirect3DPixelShader9(CONST DWORD* pFunction) : m_refCount(1) {
    if (pFunction) {
        UINT size = 0;
        while (pFunction[size] != 0x0000FFFF) size++;
        size++;
        uint32_t hash = HashBytecode(pFunction, size);
        fprintf(stderr, "CreatePixelShader: size=%d, hash=%08X, magic=%08X\n", size, hash, pFunction[0]);
        for (UINT i = 0; i < std::min(size, 16u); ++i) {
            fprintf(stderr, "  token[%d] = %08X\n", i, pFunction[i]);
        }
        fflush(stderr);
        m_function.assign(pFunction, pFunction + size);
    }
}
STDMETHODIMP GLDirect3DPixelShader9::GetFunction(void* pData, UINT* pSizeOfData) {
    if (pSizeOfData) *pSizeOfData = (UINT)(m_function.size() * sizeof(DWORD));
    if (pData) memcpy(pData, &m_function[0], m_function.size() * sizeof(DWORD));
    return D3D_OK;
}
