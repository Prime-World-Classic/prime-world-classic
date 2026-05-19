#include "stdafx.h"
#include "GLRenderer.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif

// Helper to use glBlitFramebufferEXT if glBlitFramebuffer is missing
static void NiBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) {
    typedef void (APIENTRY * PFNGLBLITFRAMEBUFFERPROC) (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
    static PFNGLBLITFRAMEBUFFERPROC pBlit = NULL;
    if (!pBlit) {
        pBlit = (PFNGLBLITFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBlitFramebuffer");
        if (!pBlit) pBlit = (PFNGLBLITFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBlitFramebufferEXT");
    }
    if (pBlit) pBlit(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
}

#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <math.h>
#include <vector>

// D3DX includes
#include "Vendor/DirectX/Include/d3dx9.h"

static int g_drawCalls = 0;
static GLuint g_currentFBO = 0;

extern void* g_sdlWindow;

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

// GLDirect3D9

GLDirect3D9::GLDirect3D9() : m_refCount(1) {}
GLDirect3D9::~GLDirect3D9() {}
STDMETHODIMP GLDirect3D9::QueryInterface(REFIID riid, void** ppvObj) { return E_NOINTERFACE; }
STDMETHODIMP_(ULONG) GLDirect3D9::AddRef() { return InterlockedIncrement(&m_refCount); }
STDMETHODIMP_(ULONG) GLDirect3D9::Release() { LONG res = InterlockedDecrement(&m_refCount); if (res == 0) { delete this; return 0; } return res; }
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
STDMETHODIMP_(ULONG) GLDirect3DDevice9::AddRef() { return InterlockedIncrement(&m_refCount); }
STDMETHODIMP_(ULONG) GLDirect3DDevice9::Release() { LONG res = InterlockedDecrement(&m_refCount); if (res == 0) { delete this; return 0; } return res; }
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
    if (g_sdlWindow) { 
        static int frames = 0; printf("Present frame %d (draw calls: %d, win: %p)\n", ++frames, g_drawCalls, g_sdlWindow); fflush(stdout);
        
        // Force window visibility every 100 frames
        if (frames % 100 == 0) { SDL_ShowWindow((SDL_Window*)g_sdlWindow); SDL_RaiseWindow((SDL_Window*)g_sdlWindow); }

        // DEBUG: Force identity viewport and draw a big triangle
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, 1024, 768);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glUseProgram(0);
        glBegin(GL_TRIANGLES);
        glColor3f(1.0f, 0.0f, 1.0f); // Bright Magenta
        glVertex2f(-0.8f, -0.8f);
        glVertex2f(0.8f, -0.8f);
        glVertex2f(0.0f, 0.8f);
        glEnd();

        SDL_GL_SwapWindow((SDL_Window*)g_sdlWindow); 
    }
    return D3D_OK;
}

STDMETHODIMP GLDirect3DDevice9::GetBackBuffer(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) { if (ppBackBuffer) { *ppBackBuffer = new GLDirect3DSurface9(); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetRasterStatus(UINT iSwapChain, D3DRASTER_STATUS* pRasterStatus) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetDialogBoxMode(BOOL bEnableDialogs) { return D3D_OK; }
STDMETHODIMP_(void) GLDirect3DDevice9::SetGammaRamp(UINT iSwapChain, DWORD Flags, CONST D3DGAMMARAMP* pRamp) {}
STDMETHODIMP_(void) GLDirect3DDevice9::GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP* pRamp) {}
STDMETHODIMP GLDirect3DDevice9::CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle) {
    if (ppTexture) { *ppTexture = new GLDirect3DTexture9(Width, Height, Levels, Usage, Format, Pool); }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle) { if (ppVertexBuffer) { *ppVertexBuffer = new GLDirect3DVertexBuffer9(Length, Usage, FVF, Pool); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle) { if (ppIndexBuffer) { *ppIndexBuffer = new GLDirect3DIndexBuffer9(Length, Usage, Format, Pool); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) { 
    if (ppSurface) { 
        GLDirect3DTexture9* tex = new GLDirect3DTexture9(Width, Height, 1, D3DUSAGE_RENDERTARGET, Format, D3DPOOL_DEFAULT);
        *ppSurface = new GLDirect3DSurface9(tex, 0); 
    } 
    return D3D_OK; 
}
STDMETHODIMP GLDirect3DDevice9::CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) { if (ppSurface) { *ppSurface = new GLDirect3DSurface9(); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::UpdateSurface(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestinationSurface, CONST POINT* pDestPoint) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::UpdateTexture(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface) { return E_NOTIMPL; }

STDMETHODIMP GLDirect3DDevice9::StretchRect(IDirect3DSurface9* pSourceSurface, CONST RECT* pSourceRect, IDirect3DSurface9* pDestSurface, CONST RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter) {
    GLuint srcFBO = 0, dstFBO = 0;
    int srcW = m_presentParams.BackBufferWidth > 0 ? m_presentParams.BackBufferWidth : 1024;
    int srcH = m_presentParams.BackBufferHeight > 0 ? m_presentParams.BackBufferHeight : 768;
    int dstW = srcW, dstH = srcH;
    if (pSourceSurface) {
        GLDirect3DSurface9* src = (GLDirect3DSurface9*)pSourceSurface;
        if (src->GetParent()) { srcFBO = src->GetParent()->GetFBO(); srcW = src->GetParent()->GetWidth(); srcH = src->GetParent()->GetHeight(); }
    }
    if (pDestSurface) {
        GLDirect3DSurface9* dst = (GLDirect3DSurface9*)pDestSurface;
        if (dst->GetParent()) { dstFBO = dst->GetParent()->GetFBO(); dstW = dst->GetParent()->GetWidth(); dstH = dst->GetParent()->GetHeight(); }
    }
    static int calls = 0; if (++calls % 60 == 0) printf("StretchRect call %d: srcFBO=%u, dstFBO=%u, size=%dx%u\n", calls, srcFBO, dstFBO, srcW, srcH);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFBO);
    int sx0 = 0, sy0 = 0, sx1 = srcW, sy1 = srcH;
    if (pSourceRect) { sx0 = pSourceRect->left; sy0 = pSourceRect->top; sx1 = pSourceRect->right; sy1 = pSourceRect->bottom; }
    int dx0 = 0, dy0 = 0, dx1 = dstW, dy1 = dstH;
    if (pDestRect) { dx0 = pDestRect->left; dy0 = pDestRect->top; dx1 = pDestRect->right; dy1 = pDestRect->bottom; }
    
    // Y-flip during blit to correct D3D vs GL coordinate differences
    NiBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy1, dx1, dy0, GL_COLOR_BUFFER_BIT, (Filter == D3DTEXF_NONE || Filter == D3DTEXF_POINT) ? GL_NEAREST : GL_LINEAR);
    
    glBindFramebuffer(GL_FRAMEBUFFER, g_currentFBO);
    return D3D_OK;
}

STDMETHODIMP GLDirect3DDevice9::ColorFill(IDirect3DSurface9* pSurface, CONST RECT* pRect, D3DCOLOR color) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) { return E_NOTIMPL; }

STDMETHODIMP GLDirect3DDevice9::SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget) { 
    printf("FORCING BACKBUFFER in SetRenderTarget\n"); fflush(stdout);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); 
    g_currentFBO = 0; 
    return D3D_OK; 
}

STDMETHODIMP GLDirect3DDevice9::GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) { if (ppRenderTarget) { *ppRenderTarget = new GLDirect3DSurface9(); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) { if (ppZStencilSurface) { *ppZStencilSurface = new GLDirect3DSurface9(); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::BeginScene() { g_drawCalls = 0; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::EndScene() { return D3D_OK; }

STDMETHODIMP GLDirect3DDevice9::Clear(DWORD Count, CONST D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) {
    float a = ((Color >> 24) & 0xFF) / 255.0f; float r = ((Color >> 16) & 0xFF) / 255.0f; float g = ((Color >> 8) & 0xFF) / 255.0f; float b = (Color & 0xFF) / 255.0f;
    static int calls = 0; if (++calls % 60 == 0) { printf("Clear call %d: color=%08X (%.2f, %.2f, %.2f), FBO=%u\n", calls, Color, r, g, b, g_currentFBO); fflush(stdout); }
    if (r == 0.0f && g == 0.0f && b == 0.0f && g_currentFBO == 0) { 
        static float t = 0; t += 0.01f;
        r = 0.2f + 0.1f * sinf(t); g = 0.2f + 0.1f * cosf(t); b = 0.4f; 
    }
    glClearColor(r, g, b, a); 
    glClearDepth(Z);
    glStencilMask(0xFF); glClearStencil(Stencil);
    GLbitfield mask = 0; if (Flags & D3DCLEAR_TARGET) mask |= GL_COLOR_BUFFER_BIT; if (Flags & D3DCLEAR_ZBUFFER) mask |= GL_DEPTH_BUFFER_BIT; if (Flags & D3DCLEAR_STENCIL) mask |= GL_STENCIL_BUFFER_BIT;
    glClear(mask); return D3D_OK;
}

STDMETHODIMP GLDirect3DDevice9::SetTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix) { 
    if (State == D3DTS_PROJECTION) { glMatrixMode(GL_PROJECTION); glLoadMatrixf((const float*)pMatrix); }
    else if (State == D3DTS_VIEW || State == D3DTS_WORLD) { glMatrixMode(GL_MODELVIEW); glLoadMatrixf((const float*)pMatrix); }
    return D3D_OK; 
}
STDMETHODIMP GLDirect3DDevice9::GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::MultiplyTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix) { return D3D_OK; }

STDMETHODIMP GLDirect3DDevice9::SetViewport(CONST D3DVIEWPORT9* pViewport) { 
    if (pViewport) {
        int h = m_presentParams.BackBufferHeight > 0 ? m_presentParams.BackBufferHeight : 768;
        glViewport(pViewport->X, h - (pViewport->Y + pViewport->Height), pViewport->Width, pViewport->Height); 
    }
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
        case D3DRS_ZENABLE: if (Value) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST); break;
        case D3DRS_ZWRITEENABLE: glDepthMask(Value ? GL_TRUE : GL_FALSE); break;
        case D3DRS_ALPHABLENDENABLE: if (Value) glEnable(GL_BLEND); else glDisable(GL_BLEND); break;
        case D3DRS_ALPHATESTENABLE: if (Value) glEnable(GL_ALPHA_TEST); else glDisable(GL_ALPHA_TEST); break;
        case D3DRS_ALPHAFUNC: {
            GLenum func = GL_ALWAYS;
            switch(Value) {
                case D3DCMP_NEVER: func = GL_NEVER; break;
                case D3DCMP_LESS: func = GL_LESS; break;
                case D3DCMP_EQUAL: func = GL_EQUAL; break;
                case D3DCMP_LESSEQUAL: func = GL_LEQUAL; break;
                case D3DCMP_GREATER: func = GL_GREATER; break;
                case D3DCMP_NOTEQUAL: func = GL_NOTEQUAL; break;
                case D3DCMP_GREATEREQUAL: func = GL_GEQUAL; break;
                case D3DCMP_ALWAYS: func = GL_ALWAYS; break;
            }
            glAlphaFunc(func, 0.0f);
            break;
        }
        case D3DRS_ALPHAREF: {
            GLint func; glGetIntegerv(GL_ALPHA_TEST_FUNC, &func);
            glAlphaFunc(func, Value / 255.0f);
            break;
        }
        case D3DRS_SRCBLEND: {
            GLenum factor = GL_ONE;
            switch(Value) {
                case D3DBLEND_ZERO: factor = GL_ZERO; break;
                case D3DBLEND_ONE: factor = GL_ONE; break;
                case D3DBLEND_SRCCOLOR: factor = GL_SRC_COLOR; break;
                case D3DBLEND_INVSRCCOLOR: factor = GL_ONE_MINUS_SRC_COLOR; break;
                case D3DBLEND_SRCALPHA: factor = GL_SRC_ALPHA; break;
                case D3DBLEND_INVSRCALPHA: factor = GL_ONE_MINUS_SRC_ALPHA; break;
                case D3DBLEND_DESTALPHA: factor = GL_DST_ALPHA; break;
                case D3DBLEND_INVDESTALPHA: factor = GL_ONE_MINUS_DST_ALPHA; break;
                case D3DBLEND_DESTCOLOR: factor = GL_DST_COLOR; break;
                case D3DBLEND_INVDESTCOLOR: factor = GL_ONE_MINUS_DST_COLOR; break;
            }
            GLint dst; glGetIntegerv(GL_BLEND_DST, &dst);
            glBlendFunc(factor, dst);
            break;
        }
        case D3DRS_DESTBLEND: {
            GLenum factor = GL_ZERO;
            switch(Value) {
                case D3DBLEND_ZERO: factor = GL_ZERO; break;
                case D3DBLEND_ONE: factor = GL_ONE; break;
                case D3DBLEND_SRCCOLOR: factor = GL_SRC_COLOR; break;
                case D3DBLEND_INVSRCCOLOR: factor = GL_ONE_MINUS_SRC_COLOR; break;
                case D3DBLEND_SRCALPHA: factor = GL_SRC_ALPHA; break;
                case D3DBLEND_INVSRCALPHA: factor = GL_ONE_MINUS_SRC_ALPHA; break;
                case D3DBLEND_DESTALPHA: factor = GL_DST_ALPHA; break;
                case D3DBLEND_INVDESTALPHA: factor = GL_ONE_MINUS_DST_ALPHA; break;
                case D3DBLEND_DESTCOLOR: factor = GL_DST_COLOR; break;
                case D3DBLEND_INVDESTCOLOR: factor = GL_ONE_MINUS_DST_COLOR; break;
            }
            GLint src; glGetIntegerv(GL_BLEND_SRC, &src);
            glBlendFunc(src, factor);
            break;
        }
        case D3DRS_BLENDOP: {
            GLenum op = GL_FUNC_ADD;
            switch(Value) {
                case D3DBLENDOP_ADD: op = GL_FUNC_ADD; break;
                case D3DBLENDOP_SUBTRACT: op = GL_FUNC_SUBTRACT; break;
                case D3DBLENDOP_REVSUBTRACT: op = GL_FUNC_REVERSE_SUBTRACT; break;
                case D3DBLENDOP_MIN: op = GL_MIN; break;
                case D3DBLENDOP_MAX: op = GL_MAX; break;
            }
            glBlendEquation(op);
            break;
        }
        case D3DRS_CULLMODE: if (Value == D3DCULL_NONE) glDisable(GL_CULL_FACE); else { glEnable(GL_CULL_FACE); glCullFace(Value == D3DCULL_CW ? GL_FRONT : GL_BACK); } break;
        case D3DRS_SCISSORTESTENABLE: if (Value) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST); break;
        case D3DRS_STENCILENABLE: if (Value) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST); break;
        case D3DRS_STENCILFUNC: {
            GLenum func = GL_ALWAYS;
            switch(Value) {
                case D3DCMP_NEVER: func = GL_NEVER; break;
                case D3DCMP_LESS: func = GL_LESS; break;
                case D3DCMP_EQUAL: func = GL_EQUAL; break;
                case D3DCMP_LESSEQUAL: func = GL_LEQUAL; break;
                case D3DCMP_GREATER: func = GL_GREATER; break;
                case D3DCMP_NOTEQUAL: func = GL_NOTEQUAL; break;
                case D3DCMP_GREATEREQUAL: func = GL_GEQUAL; break;
                case D3DCMP_ALWAYS: func = GL_ALWAYS; break;
            }
            GLint ref; glGetIntegerv(GL_STENCIL_REF, &ref);
            GLuint mask; glGetIntegerv(GL_STENCIL_VALUE_MASK, (GLint*)&mask);
            glStencilFunc(func, ref, mask);
            break;
        }
        case D3DRS_STENCILREF: {
            GLint func; glGetIntegerv(GL_STENCIL_FUNC, &func);
            GLuint mask; glGetIntegerv(GL_STENCIL_VALUE_MASK, (GLint*)&mask);
            glStencilFunc(func, Value, mask);
            break;
        }
        case D3DRS_STENCILMASK: {
            GLint func; glGetIntegerv(GL_STENCIL_FUNC, &func);
            GLint ref; glGetIntegerv(GL_STENCIL_REF, &ref);
            glStencilFunc(func, ref, Value);
            break;
        }
        case D3DRS_STENCILWRITEMASK: glStencilMask(Value); break;
        case D3DRS_STENCILPASS: {
            GLenum pass = GL_KEEP;
            switch(Value) {
                case D3DSTENCILOP_KEEP: pass = GL_KEEP; break;
                case D3DSTENCILOP_ZERO: pass = GL_ZERO; break;
                case D3DSTENCILOP_REPLACE: pass = GL_REPLACE; break;
                case D3DSTENCILOP_INCRSAT: pass = GL_INCR; break;
                case D3DSTENCILOP_DECRSAT: pass = GL_DECR; break;
                case D3DSTENCILOP_INVERT: pass = GL_INVERT; break;
                case D3DSTENCILOP_INCR: pass = GL_INCR_WRAP; break;
                case D3DSTENCILOP_DECR: pass = GL_DECR_WRAP; break;
            }
            GLint fail, zfail; glGetIntegerv(GL_STENCIL_FAIL, &fail); glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &zfail);
            glStencilOp(fail, zfail, pass);
            break;
        }
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
STDMETHODIMP GLDirect3DDevice9::SetScissorRect(CONST RECT* pRect) { 
    if (pRect) {
        int h = m_presentParams.BackBufferHeight > 0 ? m_presentParams.BackBufferHeight : 768;
        glScissor(pRect->left, h - pRect->bottom, pRect->right - pRect->left, pRect->bottom - pRect->top); 
    }
    return D3D_OK; 
}
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
        const char* vsSource = 
            "#version 120\n"
            "attribute vec4 position; attribute vec4 color; "
            "attribute vec2 texcoord0; attribute vec2 texcoord1; attribute vec2 texcoord2; attribute vec2 texcoord3;\n"
            "varying vec4 vColor; varying vec2 vTexCoord;\n"
            "uniform mat4 world; uniform mat4 view; uniform mat4 projection; uniform int is3D; uniform vec2 screenRes;\n"
            "void main() {\n"
            "  vTexCoord = texcoord0;\n"
            "  if (is3D != 0) { \n"
            "    vColor = vec4(0.0, 0.0, 0.0, 1.0); \n" // Black for 3D
            "    gl_Position = projection * view * world * vec4(position.xyz, 1.0); \n"
            "  } else { \n"
            "    vColor = vec4(1.0, 1.0, 1.0, 1.0); \n" // White for 2D
            "    float nx = (position.x / screenRes.x) * 2.0 - 1.0;\n"
            "    float ny = 1.0 - (position.y / screenRes.y) * 2.0;\n"
            "    gl_Position = vec4(nx, ny, 0.5, 1.0);\n"
            "  }\n"
            "}\n";
        const char* psSource = 
            "#version 120\n"
            "varying vec4 vColor; varying vec2 vTexCoord; uniform sampler2D tex0; uniform int useTex0;\n"
            "void main() {\n"
            "  vec4 t0 = useTex0 != 0 ? texture2D(tex0, vTexCoord) : vec4(1.0);\n"
            "  gl_FragColor = vColor * t0; gl_FragColor.a = 1.0; \n"
            "}\n";
        if (m_shaderProg) glDeleteProgram(m_shaderProg); m_shaderProg = glCreateProgram();
        GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSource); GLuint ps = CompileShader(GL_FRAGMENT_SHADER, psSource);
        glAttachShader(m_shaderProg, vs); glAttachShader(m_shaderProg, ps);
        glBindAttribLocation(m_shaderProg, 0, "position"); glBindAttribLocation(m_shaderProg, 1, "color"); 
        glBindAttribLocation(m_shaderProg, 2, "texcoord0"); glBindAttribLocation(m_shaderProg, 3, "texcoord1"); 
        glBindAttribLocation(m_shaderProg, 4, "texcoord2"); glBindAttribLocation(m_shaderProg, 5, "texcoord3");
        glLinkProgram(m_shaderProg); GLint status; glGetProgramiv(m_shaderProg, GL_LINK_STATUS, &status);
        if (!status) { char info[512]; glGetProgramInfoLog(m_shaderProg, 512, NULL, info); fprintf(stderr, "Shader link error: %s\n", info); }
        glDeleteShader(vs); glDeleteShader(ps); m_shaderDirty = false;
    }
    glUseProgram(m_shaderProg);
    
    GLint worldLoc = glGetUniformLocation(m_shaderProg, "world");
    if (worldLoc != -1) glUniformMatrix4fv(worldLoc, 1, GL_TRUE, (float*)&m_vsConstF[4]);

    GLint viewLoc = glGetUniformLocation(m_shaderProg, "view");
    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_TRUE, (float*)&m_vsConstF[8]);

    GLint projLoc = glGetUniformLocation(m_shaderProg, "projection");
    if (projLoc != -1) glUniformMatrix4fv(projLoc, 1, GL_TRUE, (float*)&m_vsConstF[12]);

    if (projLoc != -1 && m_vsConstF[12][0] == 0.0f) {
        glUniformMatrix4fv(projLoc, 1, GL_TRUE, (float*)&m_vsConstF[0]);
        static float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, identity);
        glUniformMatrix4fv(worldLoc, 1, GL_FALSE, identity);
    }

    GLint resLoc = glGetUniformLocation(m_shaderProg, "screenRes");
    float bw = m_presentParams.BackBufferWidth > 0 ? (float)m_presentParams.BackBufferWidth : 1024.0f;
    float bh = m_presentParams.BackBufferHeight > 0 ? (float)m_presentParams.BackBufferHeight : 768.0f;
    if (resLoc != -1) glUniform2f(resLoc, bw, bh);

    GLint is3DLoc = glGetUniformLocation(m_shaderProg, "is3D");
    bool hasPositionT = false;
    if (m_pVertexDecl) {
        const std::vector<D3DVERTEXELEMENT9>& elements = ((GLDirect3DVertexDeclaration9*)m_pVertexDecl)->GetElements();
        for (size_t i = 0; i < elements.size(); ++i) { if (elements[i].Usage == D3DDECLUSAGE_POSITIONT) hasPositionT = true; }
    }
    bool isRHW = hasPositionT || (m_fvf & D3DFVF_XYZRHW) != 0;
    
    if (is3DLoc != -1) glUniform1i(is3DLoc, (m_pVertexShader || !isRHW) ? 1 : 0);
    
    if (isRHW) {
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
    } else {
        glEnable(GL_DEPTH_TEST);
    }

    GLint useTex0Loc = glGetUniformLocation(m_shaderProg, "useTex0"); if (useTex0Loc != -1) glUniform1i(useTex0Loc, m_textures[0] ? 1 : 0);
    for (int i = 0; i < 8; ++i) { 
        glActiveTexture(GL_TEXTURE0 + i); 
        if (m_textures[i]) { glBindTexture(GL_TEXTURE_2D, ((GLDirect3DTexture9*)m_textures[i])->GetTex()); } 
        else { glBindTexture(GL_TEXTURE_2D, 0); } 
        char name[8]; sprintf(name, "tex%d", i);
        GLint loc = glGetUniformLocation(m_shaderProg, name);
        if (loc != -1) glUniform1i(loc, i);
    }
}

void GLDirect3DDevice9::ApplyAttributes(const void* pUPData, UINT UPStride, UINT StartVertex) {
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    for (int i = 0; i < 16; ++i) glDisableVertexAttribArray(i);
    if (m_pVertexDecl) {
        const std::vector<D3DVERTEXELEMENT9>& elements = ((GLDirect3DVertexDeclaration9*)m_pVertexDecl)->GetElements();
        for (size_t i = 0; i < elements.size(); ++i) {
            const D3DVERTEXELEMENT9& el = elements[i];
            const void* pData = NULL; UINT stride = 0;
            if (pUPData) { if (el.Stream == 0) { pData = (const char*)pUPData + el.Offset; stride = UPStride; } }
            else if (m_streams[el.Stream].pStreamData) {
                glBindBuffer(GL_ARRAY_BUFFER, ((GLDirect3DVertexBuffer9*)m_streams[el.Stream].pStreamData)->GetVBO());
                pData = (void*)(uintptr_t)(m_streams[el.Stream].OffsetInBytes + el.Offset + StartVertex * m_streams[el.Stream].Stride);
                stride = m_streams[el.Stream].Stride;
            }
            if (pData) {
                int size = 4; GLenum type = GL_FLOAT; GLboolean normalized = GL_FALSE;
                switch (el.Type) { 
                    case D3DDECLTYPE_FLOAT1: size = 1; break; case D3DDECLTYPE_FLOAT2: size = 2; break; case D3DDECLTYPE_FLOAT3: size = 3; break; case D3DDECLTYPE_FLOAT4: size = 4; break; 
                    case D3DDECLTYPE_D3DCOLOR: size = 4; type = GL_UNSIGNED_BYTE; normalized = GL_TRUE; break; 
                }
                int loc = -1;
                if (el.Usage == D3DDECLUSAGE_POSITION || el.Usage == D3DDECLUSAGE_POSITIONT) loc = 0;
                else if (el.Usage == D3DDECLUSAGE_COLOR) loc = 1;
                else if (el.Usage == D3DDECLUSAGE_TEXCOORD) loc = 2 + el.UsageIndex;
                if (loc != -1) { glEnableVertexAttribArray(loc); glVertexAttribPointer(loc, size, type, normalized, stride, pData); }
            }
        }
    } else {
        bool isRHW = (m_fvf & D3DFVF_XYZRHW) != 0;
        bool hasTex = (m_fvf & D3DFVF_TEX1) != 0;
        
        if (pUPData) {
            UINT stride = UPStride ? UPStride : (isRHW ? (hasTex ? 28 : 20) : 24);
            if (isRHW) {
                glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, pUPData);
                glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const char*)pUPData + 16);
                if (hasTex) { glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (const char*)pUPData + 20); }
            } else {
                glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, pUPData);
                glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const char*)pUPData + 12);
                if (hasTex) { glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (const char*)pUPData + 16); }
            }
        } else if (m_streams[0].pStreamData) {
            glBindBuffer(GL_ARRAY_BUFFER, ((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->GetVBO());
            UINT stride = m_streams[0].Stride ? m_streams[0].Stride : (isRHW ? (hasTex ? 28 : 20) : 24);
            void* base = (void*)(uintptr_t)(m_streams[0].OffsetInBytes + StartVertex * stride);
            if (isRHW) {
                glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, base);
                glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const char*)base + 16);
                if (hasTex) { glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (const char*)base + 20); }
            } else {
                glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, base);
                glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const char*)base + 12);
                if (hasTex) { glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (const char*)base + 16); }
            }
        }
    }
}

GLuint GLDirect3DDevice9::CompileShader(GLenum type, const char* source) {
    GLuint s = glCreateShader(type); glShaderSource(s, 1, &source, NULL); glCompileShader(s);
    GLint status; glGetShaderiv(s, GL_COMPILE_STATUS, &status);
    if (!status) { char info[512]; glGetShaderInfoLog(s, 512, NULL, info); fprintf(stderr, "Shader compile error (%s): %s\n", (type == GL_VERTEX_SHADER ? "Vertex" : "Fragment"), info); }
    else { fprintf(stderr, "Shader compile success (%s)\n", (type == GL_VERTEX_SHADER ? "Vertex" : "Fragment")); }
    return s;
}

STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) {
    g_drawCalls++; GLenum mode = GetGLPrimitiveType(PrimitiveType); GLsizei count = GetGLVertexCount(PrimitiveType, primCount);
    if (g_drawCalls < 200) { 
        printf("DrawIndexedPrimitive FBO=%u, count=%d\n", g_currentFBO, count); 
        if (m_streams[0].pStreamData) {
            float* f = (float*)((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->m_pData;
            if (f) printf("  first_v=(%.2f, %.2f, %.2f)\n", f[0], f[1], f[2]);
        }
        fflush(stdout); 
    }
    
    bool hasPositionT = false;
    if (m_pVertexDecl) {
        const std::vector<D3DVERTEXELEMENT9>& elements = ((GLDirect3DVertexDeclaration9*)m_pVertexDecl)->GetElements();
        for (size_t i = 0; i < elements.size(); ++i) { if (elements[i].Usage == D3DDECLUSAGE_POSITIONT) hasPositionT = true; }
    }
    bool isRHW = hasPositionT || (m_fvf & D3DFVF_XYZRHW) != 0;
    
    // HEURISTIC: If not explicitly RHW, but stride is 20, it's UI (Vertex2D)
    UINT stride = m_streams[0].Stride ? m_streams[0].Stride : 20;
    if (!isRHW && stride == 20) {
        isRHW = true;
    }

    if (isRHW) {
        // FORCE LEGACY PATH FOR UI
        glUseProgram(0);
        glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST);
        glDisable(GL_TEXTURE_2D); // DEBUG: Disable texturing to see geometry
        glDisable(GL_BLEND); // DEBUG: Disable blending so alpha=0 is visible
        
        // Use an explicitly wide orthographic projection since UI passes 1365 coordinates
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        glOrtho(0, 1366.0, 768.0, 0, -100.0, 100.0);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        for (int i = 0; i < 16; ++i) glDisableClientState(i);

        UINT stride = m_streams[0].Stride ? m_streams[0].Stride : 20;
        const char* base = (const char*)((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->m_pData + m_streams[0].OffsetInBytes + BaseVertexIndex * stride;
        
        GLenum indexType = GL_UNSIGNED_SHORT;
        void* indexBase = NULL;
        if (m_pIndexData) { 
            indexBase = ((GLDirect3DIndexBuffer9*)m_pIndexData)->m_pData;
            if (((GLDirect3DIndexBuffer9*)m_pIndexData)->GetFormat() == D3DFMT_INDEX32) { indexType = GL_UNSIGNED_INT; } 
        }

        if (g_drawCalls < 200) {
            int idx = indexBase ? ((unsigned short*)indexBase)[startIndex] : 0;
            float* pos = (float*)(base + idx * stride);
            printf("DrawLegacyUI call %d (FBO=%u, count=%d): v=(%.2f, %.2f, %.2f)\n", g_drawCalls, g_currentFBO, count, pos[0], pos[1], pos[2]); fflush(stdout);
        }

        glBegin(mode);
        for (int i = 0; i < count; i++) {
            int idx = i;
            if (indexBase) {
                if (indexType == GL_UNSIGNED_INT) idx = ((unsigned int*)indexBase)[startIndex + i];
                else idx = ((unsigned short*)indexBase)[startIndex + i];
            }
            const char* v = base + idx * stride;
            float* pos = (float*)v;
            unsigned char* col = (unsigned char*)(v + 16);
            // D3DCOLOR is A8R8G8B8. Little endian: B, G, R, A
            glColor4ub(col[2], col[1], col[0], 255); // R, G, B, A=255
            glVertex3f(pos[0], pos[1], 0.0f); // Ignore Z, force 0.0f to guarantee it's in front of camera
        }
        glEnd();
        return D3D_OK;
    }

    UpdateShaderProgram();
    ApplyAttributes(NULL, 0, BaseVertexIndex);
    GLenum indexType = GL_UNSIGNED_SHORT; int indexSize = 2;
    if (m_pIndexData) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ((GLDirect3DIndexBuffer9*)m_pIndexData)->GetIBO()); if (((GLDirect3DIndexBuffer9*)m_pIndexData)->GetFormat() == D3DFMT_INDEX32) { indexType = GL_UNSIGNED_INT; indexSize = 4; } }
    glDrawElements(mode, count, indexType, (void*)(uintptr_t)(startIndex * indexSize));
    glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) {
    g_drawCalls++; UpdateShaderProgram(); GLenum mode = GetGLPrimitiveType(PrimitiveType); GLsizei count = GetGLVertexCount(PrimitiveType, PrimitiveCount);
    if (g_drawCalls < 200) { printf("DrawPrimitive FBO=%u, count=%d\n", g_currentFBO, count); fflush(stdout); }
    ApplyAttributes(NULL, 0, StartVertex);
    glDrawArrays(mode, 0, count);
    glBindBuffer(GL_ARRAY_BUFFER, 0); return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) {
    g_drawCalls++; UpdateShaderProgram(); GLenum mode = GetGLPrimitiveType(PrimitiveType); GLsizei count = GetGLVertexCount(PrimitiveType, PrimitiveCount);
    if (g_drawCalls < 200 && pVertexStreamZeroData) { 
        float* f = (float*)pVertexStreamZeroData;
        printf("DrawPrimitiveUP call %d: FBO=%u, count=%d, stride=%u, first_v=(%.2f, %.2f, %.2f)\n", g_drawCalls, g_currentFBO, count, VertexStreamZeroStride, f[0], f[1], f[2]); 
        fflush(stdout); 
    }
    ApplyAttributes(pVertexStreamZeroData, VertexStreamZeroStride, 0);
    glDrawArrays(mode, 0, count); return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) {
    g_drawCalls++; UpdateShaderProgram(); GLenum mode = GetGLPrimitiveType(PrimitiveType); GLsizei count = GetGLVertexCount(PrimitiveType, PrimitiveCount);
    if (g_drawCalls < 200 && pVertexStreamZeroData) { 
        float* f = (float*)pVertexStreamZeroData;
        printf("DrawIndexedPrimitiveUP call %d: FBO=%u, count=%d, first_v=(%.2f, %.2f, %.2f)\n", g_drawCalls, g_currentFBO, count, f[0], f[1], f[2]); 
        fflush(stdout); 
    }
    GLenum indexType = (IndexDataFormat == D3DFMT_INDEX32) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    ApplyAttributes(pVertexStreamZeroData, VertexStreamZeroStride, 0);
    glDrawElements(mode, count, indexType, pIndexData); return D3D_OK; }

STDMETHODIMP GLDirect3DDevice9::ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDeclaration, DWORD Flags) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateVertexDeclaration(CONST D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl) { 
    if (ppDecl) { *ppDecl = new GLDirect3DVertexDeclaration9(pVertexElements); } 
    return D3D_OK; 
}
STDMETHODIMP GLDirect3DDevice9::SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl) { m_pVertexDecl = pDecl; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetVertexDeclaration(IDirect3DVertexDeclaration9** ppDecl) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetFVF(DWORD FVF) { m_fvf = FVF; /* printf("SetFVF: %08X\n", FVF); fflush(stdout); */ return D3D_OK; }

STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) {
    g_drawCalls++; GLenum mode = GetGLPrimitiveType(PrimitiveType); GLsizei count = GetGLVertexCount(PrimitiveType, primCount);
    
    bool hasPositionT = false;
    if (m_pVertexDecl) {
        const std::vector<D3DVERTEXELEMENT9>& elements = ((GLDirect3DVertexDeclaration9*)m_pVertexDecl)->GetElements();
        for (size_t i = 0; i < elements.size(); ++i) { if (elements[i].Usage == D3DDECLUSAGE_POSITIONT) hasPositionT = true; }
    }
    UINT stride = m_streams[0].Stride;
    bool isRHW = hasPositionT || (m_fvf & D3DFVF_XYZRHW) != 0 || stride == 20;

    if (g_drawCalls < 200) { 
        printf("DrawIndexedPrimitive FBO=%u, count=%d, stride=%u, isRHW=%d\n", g_currentFBO, count, stride, isRHW); 
        if (m_streams[0].pStreamData) {
            float* f = (float*)((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->m_pData;
            if (f) printf("  first_v=(%.2f, %.2f, %.2f)\n", f[0], f[1], f[2]);
        }
        fflush(stdout); 
    }

    if (isRHW) {
        // FORCE LEGACY PATH FOR UI
        glUseProgram(0);
        glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST);
        glDisable(GL_TEXTURE_2D); // DEBUG: Disable texturing to see geometry
        glDisable(GL_BLEND); // DEBUG: Disable blending so alpha=0 is visible
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        glOrtho(0, m_presentParams.BackBufferWidth, m_presentParams.BackBufferHeight, 0, -100.0, 100.0);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        for (int i = 0; i < 16; ++i) glDisableClientState(i);

        stride = stride ? stride : 20;
        const char* base = (const char*)((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->m_pData + m_streams[0].OffsetInBytes + BaseVertexIndex * stride;
        
        GLenum indexType = GL_UNSIGNED_SHORT;
        void* indexBase = NULL;
        if (m_pIndexData) { 
            indexBase = ((GLDirect3DIndexBuffer9*)m_pIndexData)->m_pData;
            if (((GLDirect3DIndexBuffer9*)m_pIndexData)->GetFormat() == D3DFMT_INDEX32) { indexType = GL_UNSIGNED_INT; } 
        }

        glBegin(mode);
        for (int i = 0; i < count; i++) {
            int idx = i;
            if (indexBase) {
                if (indexType == GL_UNSIGNED_INT) idx = ((unsigned int*)indexBase)[startIndex + i];
                else idx = ((unsigned short*)indexBase)[startIndex + i];
            }
            const char* v = base + idx * stride;
            float* pos = (float*)v;
            unsigned char* col = (unsigned char*)(v + 16);
            glColor4ub(col[2], col[1], col[0], 255); // ARGB -> RGBA, force opaque
            glVertex3f(pos[0], pos[1], 0.0f); // Ignore Z, force 0.0f to guarantee it's in front of camera
        }
        glEnd();
        return D3D_OK;
    }

    UpdateShaderProgram();
    ApplyAttributes(NULL, 0, BaseVertexIndex);
    GLenum indexType = GL_UNSIGNED_SHORT; int indexSize = 2;
    if (m_pIndexData) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ((GLDirect3DIndexBuffer9*)m_pIndexData)->GetIBO()); if (((GLDirect3DIndexBuffer9*)m_pIndexData)->GetFormat() == D3DFMT_INDEX32) { indexType = GL_UNSIGNED_INT; indexSize = 4; } }
    glDrawElements(mode, count, indexType, (void*)(uintptr_t)(startIndex * indexSize));
    glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); return D3D_OK;
}
