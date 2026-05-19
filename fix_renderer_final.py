import os

content = r"""#include "stdafx.h"
#include "GLRenderer.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <math.h>
#include <vector>

// D3DX includes
#include "Vendor/DirectX/Include/d3dx9.h"

static int g_drawCalls = 0;

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

class GLDirect3DTexture9;

class GLDirect3DSurface9 : public IDirect3DSurface9 {
    UINT m_width, m_height;
    D3DFORMAT m_format;
    GLDirect3DTexture9* m_pParent;
    UINT m_level;
    LONG m_refCount;
public:
    GLDirect3DSurface9(UINT w=1024, UINT h=768, D3DFORMAT fmt=D3DFMT_A8R8G8B8, GLDirect3DTexture9* parent=NULL, UINT level=0) 
        : m_width(w), m_height(h), m_format(fmt), m_pParent(parent), m_level(level), m_refCount(1) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) { return E_NOINTERFACE; }
    STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_refCount); }
    STDMETHODIMP_(ULONG) Release() { LONG res = InterlockedDecrement(&m_refCount); if (res==0) { delete this; return 0; } return res; }
    STDMETHODIMP GetDevice(IDirect3DDevice9** ppDevice) { return E_NOTIMPL; }
    STDMETHODIMP SetPrivateData(REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags) { return E_NOTIMPL; }
    STDMETHODIMP GetPrivateData(REFGUID refguid, void* pData, DWORD* pSizeOfData) { return E_NOTIMPL; }
    STDMETHODIMP FreePrivateData(REFGUID refguid) { return E_NOTIMPL; }
    STDMETHODIMP_(DWORD) SetPriority(DWORD PriorityNew) { return 0; }
    STDMETHODIMP_(DWORD) GetPriority() { return 0; }
    STDMETHODIMP_(void) PreLoad() {}
    STDMETHODIMP_(D3DRESOURCETYPE) GetType() { return D3DRTYPE_SURFACE; }
    STDMETHODIMP GetContainer(REFIID riid, void** ppContainer) { return E_NOTIMPL; }
    STDMETHODIMP GetDesc(D3DSURFACE_DESC* pDesc) { 
        if (pDesc) { pDesc->Format = m_format; pDesc->Type = D3DRTYPE_SURFACE; pDesc->Usage = 0; pDesc->Pool = D3DPOOL_DEFAULT; pDesc->MultiSampleType = D3DMULTISAMPLE_NONE; pDesc->MultiSampleQuality = 0; pDesc->Width = m_width; pDesc->Height = m_height; }
        return D3D_OK; 
    }
    STDMETHODIMP LockRect(D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags);
    STDMETHODIMP UnlockRect();
    STDMETHODIMP GetDC(HDC* phdc) { return E_NOTIMPL; }
    STDMETHODIMP ReleaseDC(HDC hdc) { return E_NOTIMPL; }
};

class GLDirect3DTexture9 : public IDirect3DTexture9 {
    GLuint m_tex;
    UINT m_width, m_height, m_levels;
    D3DFORMAT m_format;
    void* m_pData;
    LONG m_refCount;
public:
    GLDirect3DTexture9(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool) : m_tex(0), m_width(Width), m_height(Height), m_format(Format), m_levels(Levels), m_pData(NULL), m_refCount(1) {
        m_pData = malloc(Width * Height * 4); glGenTextures(1, &m_tex); glBindTexture(GL_TEXTURE_2D, m_tex); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    ~GLDirect3DTexture9() { if (m_pData) free(m_pData); if (m_tex) glDeleteTextures(1, &m_tex); }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) { return E_NOINTERFACE; }
    STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_refCount); }
    STDMETHODIMP_(ULONG) Release() { LONG res = InterlockedDecrement(&m_refCount); if (res==0) { delete this; return 0; } return res; }
    STDMETHODIMP GetDevice(IDirect3DDevice9** ppDevice) { return E_NOTIMPL; }
    STDMETHODIMP SetPrivateData(REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags) { return E_NOTIMPL; }
    STDMETHODIMP GetPrivateData(REFGUID refguid, void* pData, DWORD* pSizeOfData) { return E_NOTIMPL; }
    STDMETHODIMP FreePrivateData(REFGUID refguid) { return E_NOTIMPL; }
    STDMETHODIMP_(DWORD) SetPriority(DWORD PriorityNew) { return 0; }
    STDMETHODIMP_(DWORD) GetPriority() { return 0; }
    STDMETHODIMP_(void) PreLoad() {}
    STDMETHODIMP_(D3DRESOURCETYPE) GetType() { return D3DRTYPE_TEXTURE; }
    STDMETHODIMP_(DWORD) GetLevelCount() { return m_levels; }
    STDMETHODIMP SetLOD(DWORD LODNew) { return D3D_OK; }
    STDMETHODIMP_(DWORD) GetLOD() { return 0; }
    STDMETHODIMP GetLevelDesc(UINT Level, D3DSURFACE_DESC* pDesc) { if (pDesc) { pDesc->Format = m_format; pDesc->Type = D3DRTYPE_SURFACE; pDesc->Width = m_width; pDesc->Height = m_height; } return D3D_OK; }
    STDMETHODIMP GetSurfaceLevel(UINT Level, IDirect3DSurface9** ppSurfaceLevel) {
        if (ppSurfaceLevel) { *ppSurfaceLevel = new GLDirect3DSurface9(m_width, m_height, m_format, this, Level); }
        return D3D_OK;
    }
    STDMETHODIMP LockRect(UINT Level, D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags) { if (pLockedRect) { pLockedRect->Pitch = m_width * 4; pLockedRect->pBits = m_pData; } return D3D_OK; }
    STDMETHODIMP UnlockRect(UINT Level) {
        glBindTexture(GL_TEXTURE_2D, m_tex);
        if (m_format == 827611204) glCompressedTexImage2D(GL_TEXTURE_2D, Level, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, m_width, m_height, 0, std::max(1u, m_width/4) * std::max(1u, m_height/4) * 8, m_pData);
        else if (m_format == 894720068) glCompressedTexImage2D(GL_TEXTURE_2D, Level, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, m_width, m_height, 0, std::max(1u, m_width/4) * std::max(1u, m_height/4) * 16, m_pData);
        else glTexImage2D(GL_TEXTURE_2D, Level, GL_RGBA, m_width, m_height, 0, GL_BGRA, GL_UNSIGNED_BYTE, m_pData);
        return D3D_OK;
    }
    STDMETHODIMP AddDirtyRect(CONST RECT* pDirtyRect) { return D3D_OK; }
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

GLDirect3D9::GLDirect3D9() : m_refCount(1) {}
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
    if (pMode) { pMode->Width = 1024; pMode->Height = 768; pMode->RefreshRate = 60; pMode->Format = D3DFMT_X8R8G8B8; }
    return D3D_OK;
}
STDMETHODIMP GLDirect3D9::GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE* pMode) { return EnumAdapterModes(Adapter, D3DFMT_X8R8G8B8, 0, pMode); }
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
    if (ppReturnedDeviceInterface) { *ppReturnedDeviceInterface = new GLDirect3DDevice9(this, hFocusWindow, pPresentationParameters); }
    return D3D_OK;
}

// GLDirect3DDevice9 Implementation

void* GLDirect3DDevice9::s_sdlWindow = NULL;

GLDirect3DDevice9::GLDirect3DDevice9(IDirect3D9* pD3D, HWND hWnd, D3DPRESENT_PARAMETERS* pPresentationParameters) : m_refCount(1), m_pD3D(pD3D), m_hWnd(hWnd), m_pIndexData(NULL), m_pVertexDecl(NULL), m_fvf(0), m_pVertexShader(NULL), m_pPixelShader(NULL), m_shaderProg(0), m_shaderDirty(true) {
    if (pPresentationParameters) {
        m_presentParams = *pPresentationParameters;
        fprintf(stderr, "GLDirect3DDevice9 created: %ux%u, windowed=%d\n", m_presentParams.BackBufferWidth, m_presentParams.BackBufferHeight, m_presentParams.Windowed);
    }
    memset(m_textures, 0, sizeof(m_textures));
    memset(m_vsConstF, 0, sizeof(m_vsConstF));
    memset(m_psConstF, 0, sizeof(m_psConstF));
    memset(m_streams, 0, sizeof(m_streams));
}

GLDirect3DDevice9::~GLDirect3DDevice9() { if (m_shaderProg) glDeleteProgram(m_shaderProg); }
STDMETHODIMP GLDirect3DDevice9::QueryInterface(REFIID riid, void** ppvObj) { return E_NOINTERFACE; }
STDMETHODIMP_(ULONG) GLDirect3DDevice9::AddRef() { return InterlockedIncrement((volatile LONG*)&m_refCount); }
STDMETHODIMP_(ULONG) GLDirect3DDevice9::Release() { LONG res = InterlockedDecrement((volatile LONG*)&m_refCount); if (res == 0) { delete this; return 0; } return res; }
STDMETHODIMP GLDirect3DDevice9::TestCooperativeLevel() { return D3D_OK; }
STDMETHODIMP_(UINT) GLDirect3DDevice9::GetAvailableTextureMem() { return 1024 * 1024 * 1024; }
STDMETHODIMP GLDirect3DDevice9::EvictManagedResources() { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetDirect3D(IDirect3D9** ppD3D9) { if (ppD3D9) { *ppD3D9 = m_pD3D; m_pD3D->AddRef(); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetDeviceCaps(D3DCAPS9* pCaps) { return m_pD3D->GetDeviceCaps(0, D3DDEVTYPE_HAL, pCaps); }
STDMETHODIMP GLDirect3DDevice9::GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE* pMode) { return m_pD3D->GetAdapterDisplayMode(0, pMode); }
STDMETHODIMP GLDirect3DDevice9::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap) { return D3D_OK; }
STDMETHODIMP_(void) GLDirect3DDevice9::SetCursorPosition(int X, int Y, DWORD Flags) {}
STDMETHODIMP_(BOOL) GLDirect3DDevice9::ShowCursor(BOOL bShow) { return TRUE; }
STDMETHODIMP GLDirect3DDevice9::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::GetSwapChain(UINT iSwapChain, IDirect3DSwapChain9** pSwapChain) { return E_NOTIMPL; }
STDMETHODIMP_(UINT) GLDirect3DDevice9::GetNumberOfSwapChains() { return 1; }
STDMETHODIMP GLDirect3DDevice9::Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::Present(CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion) {
    if (s_sdlWindow) { SDL_GL_SwapWindow((SDL_Window*)s_sdlWindow); }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::GetBackBuffer(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) { if (ppBackBuffer) { *ppBackBuffer = new GLDirect3DSurface9(); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetRasterStatus(UINT iSwapChain, D3DRASTER_STATUS* pRasterStatus) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetDialogBoxMode(BOOL bEnableDialogs) { return D3D_OK; }
STDMETHODIMP_(void) GLDirect3DDevice9::SetGammaRamp(UINT iSwapChain, DWORD Flags, CONST D3DGAMMARAMP* pRAMP) {}
STDMETHODIMP_(void) GLDirect3DDevice9::GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP* pRamp) {}
STDMETHODIMP GLDirect3DDevice9::CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle) {
    if (ppTexture) { *ppTexture = new GLDirect3DTexture9(Width, Height, Levels, Usage, Format, Pool); }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle) { if (ppVertexBuffer) { *ppVertexBuffer = new GLDirect3DVertexBuffer9(Length, Usage, FVF, Pool); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle) { if (ppIndexBuffer) { *ppIndexBuffer = new GLDirect3DIndexBuffer9(Length, Usage, Format, Pool); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) { if (ppSurface) { *ppSurface = new GLDirect3DSurface9(Width, Height, Format); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) { if (ppSurface) { *ppSurface = new GLDirect3DSurface9(Width, Height, Format); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::UpdateSurface(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestinationSurface, CONST POINT* pDestPoint) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::UpdateTexture(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::StretchRect(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestSurface, CONST RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::ColorFill(IDirect3DSurface9* pSurface, CONST RECT* pRect, D3DCOLOR color) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) { if (ppRenderTarget) { *ppRenderTarget = new GLDirect3DSurface9(1024, 768, D3DFMT_A8R8G8B8); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) { if (ppZStencilSurface) { *ppZStencilSurface = new GLDirect3DSurface9(1024, 768, D3DFMT_D24S8); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::BeginScene() { g_drawCalls = 0; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::EndScene() { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::Clear(DWORD Count, CONST D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) {
    float a = ((Color >> 24) & 0xFF) / 255.0f; float r = ((Color >> 16) & 0xFF) / 255.0f; float g = ((Color >> 8) & 0xFF) / 255.0f; float b = (Color & 0xFF) / 255.0f;
    if (Color == 0) { float t = SDL_GetTicks() / 1000.0f; r = 0.5f + 0.4f * sinf(t); g = 0.5f + 0.4f * sinf(t * 1.3f); b = 0.6f + 0.3f * sinf(t * 1.7f); a = 1.0f; }
    glClearColor(r, g, b, a); GLbitfield mask = 0; if (Flags & D3DCLEAR_TARGET) mask |= GL_COLOR_BUFFER_BIT; if (Flags & D3DCLEAR_ZBUFFER) mask |= GL_DEPTH_BUFFER_BIT; if (Flags & D3DCLEAR_STENCIL) mask |= GL_STENCIL_BUFFER_BIT;
    glClear(mask); return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::SetTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix) { 
    if (State == D3DTS_PROJECTION) { glMatrixMode(GL_PROJECTION); glLoadMatrixf((const float*)pMatrix); }
    else if (State == D3DTS_VIEW || State == D3DTS_WORLD) { glMatrixMode(GL_MODELVIEW); glLoadMatrixf((const float*)pMatrix); }
    return D3D_OK; 
}
STDMETHODIMP GLDirect3DDevice9::GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::MultiplyTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::SetViewport(CONST D3DVIEWPORT9* pViewport) { if (pViewport) glViewport(pViewport->X, pViewport->Y, pViewport->Width, pViewport->Height); return D3D_OK; }
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
        case D3DRS_ZENABLE: if (Value) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST); break;
        case D3DRS_ZWRITEENABLE: glDepthMask(Value ? GL_TRUE : GL_FALSE); break;
        case D3DRS_ALPHABLENDENABLE: if (Value) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); } else glDisable(GL_BLEND); break;
        case D3DRS_CULLMODE: if (Value == D3DCULL_NONE) glDisable(GL_CULL_FACE); else { glEnable(GL_CULL_FACE); glCullFace(Value == D3DCULL_CW ? GL_FRONT : GL_BACK); } break;
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
    if (Stage < 16) m_textures[Stage] = pTexture;
    glActiveTexture(GL_TEXTURE0 + Stage);
    if (pTexture) { glBindTexture(GL_TEXTURE_2D, ((GLDirect3DTexture9*)pTexture)->GetTex()); glEnable(GL_TEXTURE_2D); }
    else { glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D); }
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
STDMETHODIMP GLDirect3DDevice9::SetScissorRect(CONST RECT* pRect) { if (pRect) glScissor(pRect->left, m_presentParams.BackBufferHeight - pRect->bottom, pRect->right - pRect->left, pRect->bottom - pRect->top); return D3D_OK; }
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

void GLDirect3DDevice9::UpdateShaderProgram() {
    if (m_shaderDirty) {
        const char* vsSource = "#version 120\nattribute vec3 position; attribute vec4 color; attribute vec2 texcoord0; varying vec4 vColor; varying vec2 vTexCoord; uniform vec4 screenRes; uniform vec4 uiCoefs; uniform vec4 vsParams[256]; uniform int is3D;\nvoid main() {\nvColor = color.bgra; vTexCoord = texcoord0;\nif (is3D != 0) { mat4 wvp = mat4(vsParams[0], vsParams[1], vsParams[2], vsParams[3]); gl_Position = vec4(position, 1.0) * wvp; }\nelse { gl_Position = vec4(position.x * uiCoefs.x + uiCoefs.z, position.y * uiCoefs.y + uiCoefs.w, 0.0, 1.0); }\n}\n";
        const char* psSource = "#version 120\nvarying vec4 vColor; varying vec2 vTexCoord; uniform sampler2D tex0; uniform int useTex0;\nvoid main() {\nvec4 t0 = texture2D(tex0, vTexCoord);\ngl_FragColor = vec4(vColor.rgb * t0.rgb, 1.0);\n}\n";
        if (m_shaderProg) glDeleteProgram(m_shaderProg); m_shaderProg = glCreateProgram();
        GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSource); GLuint ps = CompileShader(GL_FRAGMENT_SHADER, psSource);
        glAttachShader(m_shaderProg, vs); glAttachShader(m_shaderProg, ps);
        glBindAttribLocation(m_shaderProg, 0, "position"); glBindAttribLocation(m_shaderProg, 1, "color"); glBindAttribLocation(m_shaderProg, 2, "texcoord0");
        glLinkProgram(m_shaderProg); GLint status; glGetProgramiv(m_shaderProg, GL_LINK_STATUS, &status);
        if (!status) { char info[512]; glGetProgramInfoLog(m_shaderProg, 512, NULL, info); fprintf(stderr, "Shader link error: %s\n", info); }
        glDeleteShader(vs); glDeleteShader(ps); m_shaderDirty = false;
    }
    glUseProgram(m_shaderProg);
    GLint resLoc = glGetUniformLocation(m_shaderProg, "screenRes"); 
    float rw = m_presentParams.BackBufferWidth > 0 ? 2.0f / m_presentParams.BackBufferWidth : 2.0f / 1024.0f;
    float rh = m_presentParams.BackBufferHeight > 0 ? 2.0f / m_presentParams.BackBufferHeight : 2.0f / 768.0f;
    glUniform4f(resLoc, rw, rh, 0.0f, 0.0f);
    
    GLint uiCoefsLoc = glGetUniformLocation(m_shaderProg, "uiCoefs");
    if (uiCoefsLoc != -1) glUniform4fv(uiCoefsLoc, 1, (float*)&m_vsConstF[24]);

    GLint is3DLoc = glGetUniformLocation(m_shaderProg, "is3D");
    
    bool hasPositionT = false;
    if (m_pVertexDecl) {
        const std::vector<D3DVERTEXELEMENT9>& elements = ((GLDirect3DVertexDeclaration9*)m_pVertexDecl)->GetElements();
        for (size_t i = 0; i < elements.size(); ++i) { if (elements[i].Usage == D3DDECLUSAGE_POSITIONT) hasPositionT = true; }
    }
    bool isRHW = hasPositionT || (m_fvf & D3DFVF_XYZRHW) != 0;
    if (is3DLoc != -1) glUniform1i(is3DLoc, (m_pVertexShader || !isRHW) ? 1 : 0);
    
    GLint useTex0Loc = glGetUniformLocation(m_shaderProg, "useTex0"); if (useTex0Loc != -1) glUniform1i(useTex0Loc, m_textures[0] ? 1 : 0);
    for (int i = 0; i < 8; ++i) { glActiveTexture(GL_TEXTURE0 + i); if (m_textures[i]) { glBindTexture(GL_TEXTURE_2D, ((GLDirect3DTexture9*)m_textures[i])->GetTex()); char name[8]; sprintf(name, "tex%d", i); GLint texLoc = glGetUniformLocation(m_shaderProg, name); if (texLoc != -1) glUniform1i(texLoc, i); } else { glBindTexture(GL_TEXTURE_2D, 0); } }
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
}

GLuint GLDirect3DDevice9::CompileShader(GLenum type, const char* source) {
    GLuint s = glCreateShader(type); glShaderSource(s, 1, &source, NULL); glCompileShader(s);
    GLint status; glGetShaderiv(s, GL_COMPILE_STATUS, &status);
    if (!status) { char info[512]; glGetShaderInfoLog(s, 512, NULL, info); fprintf(stderr, "Shader compile error (%s): %s\n", (type == GL_VERTEX_SHADER ? "Vertex" : "Fragment"), info); }
    return s;
}

STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT StartVertex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) {
    g_drawCalls++; UpdateShaderProgram(); GLenum mode = GetGLPrimitiveType(PrimitiveType); GLsizei count = GetGLVertexCount(PrimitiveType, primCount);
    UINT curStride = m_streams[0].Stride; if (curStride == 0) curStride = 28;
    bool hasPositionT = false;
    if (m_pVertexDecl) {
        const std::vector<D3DVERTEXELEMENT9>& elements = ((GLDirect3DVertexDeclaration9*)m_pVertexDecl)->GetElements();
        for (size_t i = 0; i < elements.size(); ++i) { if (elements[i].Usage == D3DDECLUSAGE_POSITIONT) hasPositionT = true; }
    }
    bool isRHW = hasPositionT || (m_fvf & D3DFVF_XYZRHW) != 0;

    if (m_pVertexDecl) {
        const std::vector<D3DVERTEXELEMENT9>& elements = ((GLDirect3DVertexDeclaration9*)m_pVertexDecl)->GetElements();
        for (size_t i = 0; i < elements.size(); ++i) {
            const D3DVERTEXELEMENT9& el = elements[i];
            if (m_streams[el.Stream].pStreamData) {
                glBindBuffer(GL_ARRAY_BUFFER, ((GLDirect3DVertexBuffer9*)m_streams[el.Stream].pStreamData)->GetVBO());
                int size = 4; GLenum type = GL_FLOAT; GLboolean normalized = GL_FALSE;
                switch (el.Type) { case D3DDECLTYPE_FLOAT1: size = 1; break; case D3DDECLTYPE_FLOAT2: size = 2; break; case D3DDECLTYPE_FLOAT3: size = 3; break; case D3DDECLTYPE_FLOAT4: size = 4; break; case D3DDECLTYPE_D3DCOLOR: size = 4; type = GL_UNSIGNED_BYTE; normalized = GL_TRUE; break; }
                if (el.Usage == D3DDECLUSAGE_POSITION || el.Usage == D3DDECLUSAGE_POSITIONT) { glEnableVertexAttribArray(0); glVertexAttribPointer(0, size, type, normalized, m_streams[el.Stream].Stride, (void*)(uintptr_t)(el.Offset + StartVertex * m_streams[el.Stream].Stride)); }
                else if (el.Usage == D3DDECLUSAGE_COLOR) { glEnableVertexAttribArray(1); glVertexAttribPointer(1, size, type, normalized, m_streams[el.Stream].Stride, (void*)(uintptr_t)(el.Offset + StartVertex * m_streams[el.Stream].Stride)); }
                else if (el.Usage == D3DDECLUSAGE_TEXCOORD) { glEnableVertexAttribArray(2 + el.UsageIndex); glVertexAttribPointer(2 + el.UsageIndex, size, type, normalized, m_streams[el.Stream].Stride, (void*)(uintptr_t)(el.Offset + StartVertex * m_streams[el.Stream].Stride)); }
            }
        }
    } else if (m_streams[0].pStreamData) {
        glBindBuffer(GL_ARRAY_BUFFER, ((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->GetVBO());
        if (isRHW) {
            glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, curStride, (void*)(uintptr_t)(StartVertex * curStride));
            glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, curStride, (void*)(uintptr_t)(16 + StartVertex * curStride));
            glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, curStride, (void*)(uintptr_t)(20 + StartVertex * curStride));
        } else {
            glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, curStride, (void*)(uintptr_t)(StartVertex * curStride));
            glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, curStride, (void*)(uintptr_t)(12 + StartVertex * curStride));
            glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, curStride, (void*)(uintptr_t)(16 + StartVertex * curStride));
        }
    }
    GLenum indexType = GL_UNSIGNED_SHORT; int indexSize = 2;
    if (m_pIndexData) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ((GLDirect3DIndexBuffer9*)m_pIndexData)->GetIBO()); if (((GLDirect3DIndexBuffer9*)m_pIndexData)->GetFormat() == D3DFMT_INDEX32) { indexType = GL_UNSIGNED_INT; indexSize = 4; } }
    glDrawElements(mode, count, indexType, (void*)(uintptr_t)(startIndex * indexSize));
    glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) {
    g_drawCalls++; UpdateShaderProgram(); GLenum mode = GetGLPrimitiveType(PrimitiveType); GLsizei count = GetGLVertexCount(PrimitiveType, PrimitiveCount);
    UINT curStride = m_streams[0].Stride; if (curStride == 0) curStride = 28;
    bool hasPositionT = false;
    if (m_pVertexDecl) {
        const std::vector<D3DVERTEXELEMENT9>& elements = ((GLDirect3DVertexDeclaration9*)m_pVertexDecl)->GetElements();
        for (size_t i = 0; i < elements.size(); ++i) { if (elements[i].Usage == D3DDECLUSAGE_POSITIONT) hasPositionT = true; }
    }
    bool isRHW = hasPositionT || (m_fvf & D3DFVF_XYZRHW) != 0;
    if (m_pVertexDecl) {
        const std::vector<D3DVERTEXELEMENT9>& elements = ((GLDirect3DVertexDeclaration9*)m_pVertexDecl)->GetElements();
        for (size_t i = 0; i < elements.size(); ++i) {
            const D3DVERTEXELEMENT9& el = elements[i];
            if (m_streams[el.Stream].pStreamData) {
                glBindBuffer(GL_ARRAY_BUFFER, ((GLDirect3DVertexBuffer9*)m_streams[el.Stream].pStreamData)->GetVBO());
                int size = 4; GLenum type = GL_FLOAT; GLboolean normalized = GL_FALSE;
                switch (el.Type) { case D3DDECLTYPE_FLOAT1: size = 1; break; case D3DDECLTYPE_FLOAT2: size = 2; break; case D3DDECLTYPE_FLOAT3: size = 3; break; case D3DDECLTYPE_FLOAT4: size = 4; break; case D3DDECLTYPE_D3DCOLOR: size = 4; type = GL_UNSIGNED_BYTE; normalized = GL_TRUE; break; }
                if (el.Usage == D3DDECLUSAGE_POSITION || el.Usage == D3DDECLUSAGE_POSITIONT) { glEnableVertexAttribArray(0); glVertexAttribPointer(0, size, type, normalized, m_streams[el.Stream].Stride, (void*)(uintptr_t)(el.Offset + StartVertex * m_streams[el.Stream].Stride)); }
                else if (el.Usage == D3DDECLUSAGE_COLOR) { glEnableVertexAttribArray(1); glVertexAttribPointer(1, size, type, normalized, m_streams[el.Stream].Stride, (void*)(uintptr_t)(el.Offset + StartVertex * m_streams[el.Stream].Stride)); }
                else if (el.Usage == D3DDECLUSAGE_TEXCOORD) { glEnableVertexAttribArray(2 + el.UsageIndex); glVertexAttribPointer(2 + el.UsageIndex, size, type, normalized, m_streams[el.Stream].Stride, (void*)(uintptr_t)(el.Offset + StartVertex * m_streams[el.Stream].Stride)); }
            }
        }
    } else if (m_streams[0].pStreamData) { 
        glBindBuffer(GL_ARRAY_BUFFER, ((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->GetVBO());
        if (isRHW) {
            glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, curStride, (void*)(uintptr_t)(StartVertex * curStride));
            glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, curStride, (void*)(uintptr_t)(16 + StartVertex * curStride));
            glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, curStride, (void*)(uintptr_t)(20 + StartVertex * curStride));
        } else {
            glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, curStride, (void*)(uintptr_t)(StartVertex * curStride));
            glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, curStride, (void*)(uintptr_t)(12 + StartVertex * curStride));
            glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, curStride, (void*)(uintptr_t)(16 + StartVertex * curStride));
        }
    }
    glDrawArrays(mode, StartVertex, count); return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) {
    GLenum mode = GetGLPrimitiveType(PrimitiveType); GLsizei count = GetGLVertexCount(PrimitiveType, PrimitiveCount);
    glBindBuffer(GL_ARRAY_BUFFER, 0); glDrawArrays(mode, 0, count); return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) {
    GLenum mode = GetGLPrimitiveType(PrimitiveType); GLsizei count = GetGLVertexCount(PrimitiveType, PrimitiveCount);
    GLenum indexType = (IndexDataFormat == D3DFMT_INDEX32) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); glDrawElements(mode, count, indexType, pIndexData); return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDeclaration, DWORD Flags) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateVertexDeclaration(CONST D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl) { 
    if (ppDecl) { *ppDecl = new GLDirect3DVertexDeclaration9(pVertexElements); } 
    return D3D_OK; 
}
STDMETHODIMP GLDirect3DDevice9::SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl) { m_pVertexDecl = pDecl; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetVertexDeclaration(IDirect3DVertexDeclaration9** ppDecl) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetFVF(DWORD FVF) { m_fvf = FVF; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetFVF(DWORD* pFVF) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::CreateVertexShader(CONST DWORD* pFunction, IDirect3DVertexShader9** ppShader) { if (ppShader) { *ppShader = new GLDirect3DVertexShader9(pFunction); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::SetVertexShader(IDirect3DVertexShader9* pShader) { m_pVertexShader = pShader; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetVertexShader(IDirect3DVertexShader9** ppShader) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetVertexShaderConstantF(UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount) { 
    if (StartRegister + Vector4fCount <= 256) { memcpy(&m_vsConstF[StartRegister], pConstantData, Vector4fCount * 16); } return D3D_OK; 
}
STDMETHODIMP GLDirect3DDevice9::GetVertexShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetVertexShaderConstantI(UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetVertexShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetVertexShaderConstantB(UINT StartRegister, CONST BOOL* pConstantData, UINT  BoolCount) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetVertexShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::CreatePixelShader(CONST DWORD* pFunction, IDirect3DPixelShader9** ppShader) { if (ppShader) { *ppShader = new GLDirect3DPixelShader9(pFunction); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::SetPixelShader(IDirect3DPixelShader9* pShader) { m_pPixelShader = pShader; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetPixelShader(IDirect3DPixelShader9** ppShader) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetPixelShaderConstantF(UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount) { if (StartRegister + Vector4fCount <= 256) { memcpy(&m_psConstF[StartRegister], pConstantData, Vector4fCount * 16); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetPixelShaderConstantF(UINT StartRegister, float* pConstantData, UINT Vector4fCount) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetPixelShaderConstantI(UINT StartRegister, CONST int* pConstantData, UINT Vector4iCount) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetPixelShaderConstantI(UINT StartRegister, int* pConstantData, UINT Vector4iCount) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetPixelShaderConstantB(UINT StartRegister, CONST BOOL* pConstantData, UINT  BoolCount) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetPixelShaderConstantB(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::DrawRectPatch(UINT Handle, CONST float* pNumSegs, CONST D3DRECTPATCH_INFO* pPatchInfo) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::DrawTriPatch(UINT Handle, CONST float* pNumSegs, CONST D3DTRIPATCH_INFO* pPatchInfo) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::DeletePatch(UINT Handle) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery) { if (ppQuery) { *ppQuery = new GLDirect3DQuery9(); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) { 
    if (StreamNumber < 16) { m_streams[StreamNumber].pStreamData = pStreamData; m_streams[StreamNumber].OffsetInBytes = OffsetInBytes; m_streams[StreamNumber].Stride = Stride; } 
    return D3D_OK; 
}
STDMETHODIMP GLDirect3DDevice9::GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* OffsetInBytes, UINT* pStride) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetStreamSourceFreq(UINT StreamNumber, UINT Setting) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetStreamSourceFreq(UINT StreamNumber, UINT* pSetting) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetIndices(IDirect3DIndexBuffer9* pIndexData) { m_pIndexData = pIndexData; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetIndices(IDirect3DIndexBuffer9** ppIndexData) { return E_NOTIMPL; }

GLDirect3DVertexBuffer9::GLDirect3DVertexBuffer9(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool) : m_length(Length), m_pData(NULL), m_vbo(0), m_refCount(1) { m_pData = malloc(Length); glGenBuffers(1, &m_vbo); }
GLDirect3DVertexBuffer9::~GLDirect3DVertexBuffer9() { if (m_pData) free(m_pData); if (m_vbo) glDeleteBuffers(1, &m_vbo); }
STDMETHODIMP GLDirect3DVertexBuffer9::Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) { if (ppbData) *ppbData = (char*)m_pData + OffsetToLock; return D3D_OK; }
STDMETHODIMP GLDirect3DVertexBuffer9::Unlock() { glBindBuffer(GL_ARRAY_BUFFER, m_vbo); glBufferData(GL_ARRAY_BUFFER, m_length, m_pData, GL_DYNAMIC_DRAW); return D3D_OK; }
STDMETHODIMP GLDirect3DVertexBuffer9::GetDesc(D3DVERTEXBUFFER_DESC *pDesc) { return E_NOTIMPL; }

GLDirect3DIndexBuffer9::GLDirect3DIndexBuffer9(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool) : m_length(Length), m_format(Format), m_pData(NULL), m_ibo(0), m_refCount(1) { m_pData = malloc(Length); glGenBuffers(1, &m_ibo); }
GLDirect3DIndexBuffer9::~GLDirect3DIndexBuffer9() { if (m_pData) free(m_pData); if (m_ibo) glDeleteBuffers(1, &m_ibo); }
STDMETHODIMP GLDirect3DIndexBuffer9::Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) { if (ppbData) *ppbData = (char*)m_pData + OffsetToLock; return D3D_OK; }
STDMETHODIMP GLDirect3DIndexBuffer9::Unlock() { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_length, m_pData, GL_DYNAMIC_DRAW); return D3D_OK; }
STDMETHODIMP GLDirect3DIndexBuffer9::GetDesc(D3DINDEXBUFFER_DESC *pDesc) { return E_NOTIMPL; }

GLDirect3DVertexDeclaration9::GLDirect3DVertexDeclaration9(CONST D3DVERTEXELEMENT9* pVertexElements) : m_refCount(1) { while (pVertexElements->Stream != 0xFF) { m_elements.push_back(*pVertexElements); pVertexElements++; } }
STDMETHODIMP GLDirect3DVertexDeclaration9::GetDeclaration(D3DVERTEXELEMENT9* pElement, UINT* pNumElements) { if (pNumElements) *pNumElements = (UINT)m_elements.size(); if (pElement) { for (size_t i = 0; i < m_elements.size(); ++i) pElement[i] = m_elements[i]; } return D3D_OK; }
GLDirect3DVertexShader9::GLDirect3DVertexShader9(CONST DWORD* pFunction) : m_refCount(1) { if (pFunction) { UINT size = 0; while (pFunction[size] != 0x0000FFFF) size++; size++; m_function.assign(pFunction, pFunction + size); } }
STDMETHODIMP GLDirect3DVertexShader9::GetFunction(void* pData, UINT* pSizeOfData) { if (pSizeOfData) *pSizeOfData = (UINT)(m_function.size() * sizeof(DWORD)); if (pData) memcpy(pData, &m_function[0], m_function.size() * sizeof(DWORD)); return D3D_OK; }
GLDirect3DPixelShader9::GLDirect3DPixelShader9(CONST DWORD* pFunction) : m_refCount(1) { if (pFunction) { UINT size = 0; while (pFunction[size] != 0x0000FFFF) size++; size++; m_function.assign(pFunction, pFunction + size); } }
STDMETHODIMP GLDirect3DPixelShader9::GetFunction(void* pData, UINT* pSizeOfData) { if (pSizeOfData) *pSizeOfData = (UINT)(m_function.size() * sizeof(DWORD)); if (pData) memcpy(pData, &m_function[0], m_function.size() * sizeof(DWORD)); return D3D_OK; }
"""

with open('pw/branches/r1117/Src/Render/GLRenderer.cpp', 'w') as f:
    f.write(content)
