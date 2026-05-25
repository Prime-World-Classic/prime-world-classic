import sys

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
#include "Vendor/DirectX/Include/d3dx9.h"

static int g_drawCalls = 0;
static GLuint g_currentFBO = 0;
extern "C" void* g_sdlWindow;

extern "C" IDirect3D9 * WINAPI Direct3DCreate9(UINT SDKVersion) { return new GLDirect3D9(); }
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

static void NiBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) {
    typedef void (APIENTRY * PFNGLBLITFRAMEBUFFERPROC) (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
    static PFNGLBLITFRAMEBUFFERPROC pBlit = NULL;
    if (!pBlit) { pBlit = (PFNGLBLITFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBlitFramebuffer"); if (!pBlit) pBlit = (PFNGLBLITFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBlitFramebufferEXT"); }
    if (pBlit) pBlit(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
}

GLDirect3D9::GLDirect3D9() : m_refCount(1) {}
GLDirect3D9::~GLDirect3D9() {}
STDMETHODIMP GLDirect3D9::QueryInterface(REFIID riid, void** ppvObj) { return E_NOINTERFACE; }
STDMETHODIMP_(ULONG) GLDirect3D9::AddRef() { return ++m_refCount; }
STDMETHODIMP_(ULONG) GLDirect3D9::Release() { if (--m_refCount == 0) { delete this; return 0; } return m_refCount; }
STDMETHODIMP GLDirect3D9::RegisterSoftwareDevice(void* pInit) { return E_NOTIMPL; }
STDMETHODIMP_(UINT) GLDirect3D9::GetAdapterCount() { return 1; }
STDMETHODIMP GLDirect3D9::GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pId) { if (pId) { memset(pId, 0, sizeof(*pId)); strncpy(pId->Description, "GL", 31); } return D3D_OK; }
STDMETHODIMP_(UINT) GLDirect3D9::GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) { return 1; }
STDMETHODIMP GLDirect3D9::EnumAdapterModes(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode) { if (pMode) { pMode->Width=1024; pMode->Height=768; pMode->RefreshRate=60; pMode->Format=D3DFMT_X8R8G8B8; } return D3D_OK; }
STDMETHODIMP GLDirect3D9::GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE* pMode) { return EnumAdapterModes(0, D3DFMT_X8R8G8B8, 0, pMode); }
STDMETHODIMP GLDirect3D9::CheckDeviceType(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdFmt, D3DFORMAT BbFmt, BOOL bWin) { return D3D_OK; }
STDMETHODIMP GLDirect3D9::CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdFmt, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) { return D3D_OK; }
STDMETHODIMP GLDirect3D9::CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT SurfFmt, BOOL Win, D3DMULTISAMPLE_TYPE MSType, DWORD* pQual) { return D3D_OK; }
STDMETHODIMP GLDirect3D9::CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdFmt, D3DFORMAT RtFmt, D3DFORMAT DsFmt) { return D3D_OK; }
STDMETHODIMP GLDirect3D9::CheckDeviceFormatConversion(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT SrcFmt, D3DFORMAT TgtFmt) { return D3D_OK; }
STDMETHODIMP GLDirect3D9::GetDeviceCaps(UINT Adapter, D3DDEVTYPE DevType, D3DCAPS9* pCaps) { if (pCaps) { memset(pCaps, 0, sizeof(*pCaps)); pCaps->DeviceType=D3DDEVTYPE_HAL; pCaps->PixelShaderVersion=D3DPS_VERSION(3,0); pCaps->VertexShaderVersion=D3DVS_VERSION(3,0); } return D3D_OK; }
STDMETHODIMP_(HMONITOR) GLDirect3D9::GetAdapterMonitor(UINT Adapter) { return (HMONITOR)1; }
STDMETHODIMP GLDirect3D9::CreateDevice(UINT Adapter, D3DDEVTYPE DevType, HWND hWnd, DWORD Flags, D3DPRESENT_PARAMETERS* pPP, IDirect3DDevice9** ppDev) { if (ppDev) *ppDev = new GLDirect3DDevice9(this, hWnd, pPP); return D3D_OK; }

GLDirect3DDevice9::GLDirect3DDevice9(IDirect3D9* pD3D, HWND hWnd, D3DPRESENT_PARAMETERS* pPP) : m_pD3D(pD3D), m_hWnd(hWnd), m_pIndexData(NULL), m_pVertexDecl(NULL), m_fvf(0), m_pVertexShader(NULL), m_pPixelShader(NULL), m_shaderProg(0), m_shaderDirty(true) {
    m_refCount = 1; if (pPP) m_presentParams = *pPP;
    memset(m_textures, 0, sizeof(m_textures)); memset(m_vsConstF, 0, sizeof(m_vsConstF)); memset(m_psConstF, 0, sizeof(m_psConstF)); memset(m_streams, 0, sizeof(m_streams));
}
GLDirect3DDevice9::~GLDirect3DDevice9() { if (m_shaderProg) glDeleteProgram(m_shaderProg); }
STDMETHODIMP GLDirect3DDevice9::QueryInterface(REFIID riid, void** ppvObj) { return E_NOINTERFACE; }
STDMETHODIMP_(ULONG) GLDirect3DDevice9::AddRef() { return ++m_refCount; }
STDMETHODIMP_(ULONG) GLDirect3DDevice9::Release() { if (--m_refCount == 0) { delete this; return 0; } return m_refCount; }
STDMETHODIMP GLDirect3DDevice9::TestCooperativeLevel() { return D3D_OK; }
STDMETHODIMP_(UINT) GLDirect3DDevice9::GetAvailableTextureMem() { return 1024*1024*1024; }
STDMETHODIMP GLDirect3DDevice9::EvictManagedResources() { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetDirect3D(IDirect3D9** ppD3D) { if (ppD3D) { *ppD3D=m_pD3D; m_pD3D->AddRef(); } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetDeviceCaps(D3DCAPS9* pCaps) { return m_pD3D->GetDeviceCaps(0, D3DDEVTYPE_HAL, pCaps); }
STDMETHODIMP GLDirect3DDevice9::GetDisplayMode(UINT iSC, D3DDISPLAYMODE* pMode) { return m_pD3D->GetAdapterDisplayMode(0, pMode); }
STDMETHODIMP GLDirect3DDevice9::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* p) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetCursorProperties(UINT x, UINT y, IDirect3DSurface9* pBmp) { return D3D_OK; }
STDMETHODIMP_(void) GLDirect3DDevice9::SetCursorPosition(int x, int y, DWORD f) {}
STDMETHODIMP_(BOOL) GLDirect3DDevice9::ShowCursor(BOOL b) { return TRUE; }
STDMETHODIMP GLDirect3DDevice9::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* p, IDirect3DSwapChain9** s) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::GetSwapChain(UINT i, IDirect3DSwapChain9** s) { return E_NOTIMPL; }
STDMETHODIMP_(UINT) GLDirect3DDevice9::GetNumberOfSwapChains() { return 1; }
STDMETHODIMP GLDirect3DDevice9::Reset(D3DPRESENT_PARAMETERS* p) { return D3D_OK; }

STDMETHODIMP GLDirect3DDevice9::Present(CONST RECT* pSrc, CONST RECT* pDst, HWND hWnd, CONST RGNDATA* pReg) {
    if (g_sdlWindow) {
        static int frames = 0; if (++frames % 60 == 0) { printf("Present frame %d\\n", frames); fflush(stdout); }
        for (GLuint fbo = 1; fbo <= 5; fbo++) {
            if (glIsFramebuffer(fbo)) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                glBlitFramebuffer(0, 0, 1024, 768, 0, 0, 1024, 768, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                glGetError();
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0); 
        SDL_GL_SwapWindow((SDL_Window*)g_sdlWindow);
    }
    return D3D_OK;
}

STDMETHODIMP GLDirect3DDevice9::GetBackBuffer(UINT iSC, UINT iBB, D3DBACKBUFFER_TYPE t, IDirect3DSurface9** ppBB) { if (ppBB) *ppBB = new GLDirect3DSurface9(NULL, 0); return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetRasterStatus(UINT iSC, D3DRASTER_STATUS* p) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetDialogBoxMode(BOOL b) { return D3D_OK; }
STDMETHODIMP_(void) GLDirect3DDevice9::SetGammaRamp(UINT iSC, DWORD f, CONST D3DGAMMARAMP* p) {}
STDMETHODIMP_(void) GLDirect3DDevice9::GetGammaRamp(UINT iSC, D3DGAMMARAMP* p) {}
STDMETHODIMP GLDirect3DDevice9::CreateTexture(UINT w, UINT h, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DTexture9** ppT, HANDLE* ph) { if (ppT) *ppT = new GLDirect3DTexture9(w, h, l, u, f, p); return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateVolumeTexture(UINT w, UINT h, UINT d, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DVolumeTexture9** ppT, HANDLE* ph) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::CreateCubeTexture(UINT e, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DCubeTexture9** ppT, HANDLE* ph) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::CreateVertexBuffer(UINT len, DWORD u, DWORD fvf, D3DPOOL p, IDirect3DVertexBuffer9** ppVB, HANDLE* ph) { if (ppVB) *ppVB = new GLDirect3DVertexBuffer9(len, u, fvf, p); return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateIndexBuffer(UINT len, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DIndexBuffer9** ppIB, HANDLE* ph) { if (ppIB) *ppIB = new GLDirect3DIndexBuffer9(len, u, f, p); return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateRenderTarget(UINT w, UINT h, D3DFORMAT f, D3DMULTISAMPLE_TYPE ms, DWORD q, BOOL l, IDirect3DSurface9** ppS, HANDLE* ph) { if (ppS) *ppS = new GLDirect3DSurface9(new GLDirect3DTexture9(w, h, 1, D3DUSAGE_RENDERTARGET, f, D3DPOOL_DEFAULT), 0); return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateDepthStencilSurface(UINT w, UINT h, D3DFORMAT f, D3DMULTISAMPLE_TYPE ms, DWORD q, BOOL d, IDirect3DSurface9** ppS, HANDLE* ph) { if (ppS) *ppS = new GLDirect3DSurface9(NULL, 0); return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::UpdateSurface(IDirect3DSurface9* pS, CONST RECT* pSR, IDirect3DSurface9* pD, CONST POINT* pDP) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::UpdateTexture(IDirect3DBaseTexture9* pS, IDirect3DBaseTexture9* pD) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetRenderTargetData(IDirect3DSurface9* pR, IDirect3DSurface9* pD) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::GetFrontBufferData(UINT iSC, IDirect3DSurface9* pD) { return E_NOTIMPL; }

STDMETHODIMP GLDirect3DDevice9::StretchRect(IDirect3DSurface9* pS, CONST RECT* pSR, IDirect3DSurface9* pD, CONST RECT* pDR, D3DTEXTUREFILTERTYPE f) {
    GLuint sFBO = 0, dFBO = 0; int sw = 1024, sh = 768, dw = 1024, dh = 768;
    if (pS) { GLDirect3DSurface9* s_ptr = (GLDirect3DSurface9*)pS; if (s_ptr->GetParent()) { sFBO = s_ptr->GetParent()->GetFBO(); sw = s_ptr->GetParent()->GetWidth(); sh = s_ptr->GetParent()->GetHeight(); } }
    if (pD) { GLDirect3DSurface9* d_ptr = (GLDirect3DSurface9*)pD; if (d_ptr->GetParent()) { dFBO = d_ptr->GetParent()->GetFBO(); dw = d_ptr->GetParent()->GetWidth(); dh = d_ptr->GetParent()->GetHeight(); } }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sFBO); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dFBO);
    int sx0=0, sy0=0, sx1=sw, sy1=sh; if (pSR) { sx0=pSR->left; sy0=pSR->top; sx1=pSR->right; sy1=pSR->bottom; }
    int dx0=0, dy0=0, dx1=dw, dy1=dh; if (pDR) { dx0=pDR->left; dy0=pDR->top; dx1=pDR->right; dy1=pDR->bottom; }
    NiBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy1, dx1, dy0, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, g_currentFBO); return D3D_OK;
}

STDMETHODIMP GLDirect3DDevice9::ColorFill(IDirect3DSurface9* pS, CONST RECT* pR, D3DCOLOR c) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateOffscreenPlainSurface(UINT w, UINT h, D3DFORMAT f, D3DPOOL p, IDirect3DSurface9** ppS, HANDLE* ph) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetRenderTarget(DWORD i, IDirect3DSurface9* pRt) { if (pRt) { GLDirect3DSurface9* s_ptr = (GLDirect3DSurface9*)pRt; if (s_ptr->GetParent()) { g_currentFBO = s_ptr->GetParent()->GetFBO(); glBindFramebuffer(GL_FRAMEBUFFER, g_currentFBO); glViewport(0, 0, s_ptr->GetParent()->GetWidth(), s_ptr->GetParent()->GetHeight()); } else { g_currentFBO=0; glBindFramebuffer(GL_FRAMEBUFFER,0); } } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetRenderTarget(DWORD i, IDirect3DSurface9** r) { if (r) *r = new GLDirect3DSurface9(NULL, 0); return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::SetDepthStencilSurface(IDirect3DSurface9* p) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetDepthStencilSurface(IDirect3DSurface9** p) { if (p) *p = new GLDirect3DSurface9(NULL, 0); return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::BeginScene() { g_drawCalls = 0; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::EndScene() { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::Clear(DWORD c, CONST D3DRECT* r_ptr, DWORD f, D3DCOLOR col, float z, DWORD s_val) {
    float _a=((col>>24)&0xFF)/255.f, _r=((col>>16)&0xFF)/255.f, _g=((col>>8)&0xFF)/255.f, _b=(col&0xFF)/255.f;
    glClearColor(_r,_g,_b,_a); glClearDepth(z); glClearStencil(s_val);
    GLbitfield m=0; if(f&D3DCLEAR_TARGET) m|=GL_COLOR_BUFFER_BIT; if(f&D3DCLEAR_ZBUFFER) m|=GL_DEPTH_BUFFER_BIT; if(f&D3DCLEAR_STENCIL) m|=GL_STENCIL_BUFFER_BIT;
    glClear(m); return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::SetTransform(D3DTRANSFORMSTATETYPE s, CONST D3DMATRIX* m) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetTransform(D3DTRANSFORMSTATETYPE s, D3DMATRIX* m) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::MultiplyTransform(D3DTRANSFORMSTATETYPE s, CONST D3DMATRIX* m) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::SetViewport(CONST D3DVIEWPORT9* v) { if(v) glViewport(v->X, 768-(v->Y+v->Height), v->Width, v->Height); return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetViewport(D3DVIEWPORT9* v) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetMaterial(CONST D3DMATERIAL9* m) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetMaterial(D3DMATERIAL9* m) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetLight(DWORD i, CONST D3DLIGHT9* l) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetLight(DWORD i, D3DLIGHT9* l) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::LightEnable(DWORD i, BOOL e) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetLightEnable(DWORD i, BOOL* e) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetClipPlane(DWORD i, CONST float* p) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetClipPlane(DWORD i, float* p) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetRenderState(D3DRENDERSTATETYPE s, DWORD v) {
    if(s==D3DRS_ZENABLE) { if(v) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST); }
    if(s==D3DRS_ALPHABLENDENABLE) { if(v) glEnable(GL_BLEND); else glDisable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::GetRenderState(D3DRENDERSTATETYPE s, DWORD* v) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::CreateStateBlock(D3DSTATEBLOCKTYPE t, IDirect3DStateBlock9** s) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::BeginStateBlock() { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::EndStateBlock(IDirect3DStateBlock9** s) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetClipStatus(CONST D3DCLIPSTATUS9* c) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetClipStatus(D3DCLIPSTATUS9* c) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::GetTexture(DWORD s, IDirect3DBaseTexture9** t) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetTexture(DWORD s, IDirect3DBaseTexture9* t) { if(s<16) m_textures[s]=t; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetTextureStageState(DWORD s, D3DTEXTURESTAGESTATETYPE t, DWORD* v) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetTextureStageState(DWORD s, D3DTEXTURESTAGESTATETYPE t, DWORD v) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetSamplerState(DWORD s, D3DSAMPLERSTATETYPE t, DWORD* v) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetSamplerState(DWORD s, D3DSAMPLERSTATETYPE t, DWORD v) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::ValidateDevice(DWORD* n) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::SetPaletteEntries(UINT n, CONST PALETTEENTRY* p) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetPaletteEntries(UINT n, PALETTEENTRY* p) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetCurrentTexturePalette(UINT n) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetCurrentTexturePalette(UINT *n) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetScissorRect(CONST RECT* p) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetScissorRect(RECT* r) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetSoftwareVertexProcessing(BOOL s) { return D3D_OK; }
STDMETHODIMP_(BOOL) GLDirect3DDevice9::GetSoftwareVertexProcessing() { return TRUE; }
STDMETHODIMP GLDirect3DDevice9::SetNPatchMode(float n) { return D3D_OK; }
STDMETHODIMP_(float) GLDirect3DDevice9::GetNPatchMode() { return 0.0f; }

static GLenum GetGLPrim(D3DPRIMITIVETYPE t) {
    if(t==D3DPT_LINELIST) return GL_LINES; if(t==D3DPT_LINESTRIP) return GL_LINE_STRIP;
    if(t==D3DPT_TRIANGLESTRIP) return GL_TRIANGLE_STRIP; if(t==D3DPT_TRIANGLEFAN) return GL_TRIANGLE_FAN;
    return GL_TRIANGLES;
}
static GLsizei GetGLCount(D3DPRIMITIVETYPE t, UINT p) {
    if(t==D3DPT_LINELIST) return p*2; if(t==D3DPT_LINESTRIP) return p+1;
    if(t==D3DPT_TRIANGLELIST) return p*3; if(t==D3DPT_TRIANGLESTRIP) return p+2;
    if(t==D3DPT_TRIANGLEFAN) return p+2; return p;
}

void GLDirect3DDevice9::UpdateShaderProgram(bool isRHW) {
    if (m_shaderDirty) {
        const char* vs = "#version 120\nattribute vec4 position; attribute vec4 color; attribute vec2 texcoord0; varying vec4 vColor; varying vec2 vTexCoord; uniform int is3D; uniform vec2 screenRes;\nvoid main() {\nvTexCoord = texcoord0; vColor = color.bgra; if(vColor.a < 0.01) vColor.a = 1.0;\nif (is3D != 0) { gl_Position = vec4(position.xyz, 1.0); }\nelse { gl_Position = vec4((position.x/1366.0)*2.0-1.0, 1.0-(position.y/768.0)*2.0, 0.0, 1.0); }\n}\n";
        const char* ps = "#version 120\nvarying vec4 vColor; varying vec2 vTexCoord; uniform sampler2D tex0; uniform int useTex0;\nvoid main() {\nvec4 t0 = useTex0 != 0 ? texture2D(tex0, vTexCoord) : vec4(1.0);\ngl_FragColor = vColor * t0;\n}\n";
        if (m_shaderProg) glDeleteProgram(m_shaderProg); m_shaderProg = glCreateProgram();
        GLuint v = CompileShader(GL_VERTEX_SHADER, vs); GLuint p = CompileShader(GL_FRAGMENT_SHADER, ps);
        glAttachShader(m_shaderProg, v); glAttachShader(m_shaderProg, p);
        glBindAttribLocation(m_shaderProg, 0, "position"); glBindAttribLocation(m_shaderProg, 1, "color"); glBindAttribLocation(m_shaderProg, 2, "texcoord0");
        glLinkProgram(m_shaderProg); m_shaderDirty = false;
    }
    glUseProgram(m_shaderProg);
    GLint is3DLoc = glGetUniformLocation(m_shaderProg, "is3D"); if (is3DLoc != -1) glUniform1i(is3DLoc, isRHW ? 0 : 1);
    GLint resLoc = glGetUniformLocation(m_shaderProg, "screenRes"); if (resLoc != -1) glUniform2f(resLoc, 1024.f, 768.f);
    GLint useTexLoc = glGetUniformLocation(m_shaderProg, "useTex0"); if (useTexLoc != -1) glUniform1i(useTexLoc, m_textures[0] ? 1 : 0);
    for(int i=0; i<8; i++) { glActiveTexture(GL_TEXTURE0+i); if(m_textures[i]) glBindTexture(GL_TEXTURE_2D, ((GLDirect3DTexture9*)m_textures[i])->GetTex()); else glBindTexture(GL_TEXTURE_2D, 0); }
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (isRHW) { glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST); } else { glEnable(GL_DEPTH_TEST); }
}

void GLDirect3DDevice9::ApplyAttributes(const void* pUP, UINT ups, UINT startV) {
    for(int i=0; i<16; i++) glDisableVertexAttribArray(i);
    if (pUP) {
        UINT s = ups ? ups : 20; glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, 0, s, pUP);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, 1, s, (const char*)pUP+16);
        if (s >= 28) { glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, 0, s, (const char*)pUP+20); }
    } else if (m_streams[0].pStreamData) {
        UINT s = m_streams[0].Stride ? m_streams[0].Stride : 20;
        glBindBuffer(GL_ARRAY_BUFFER, ((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->GetVBO());
        const char* b = (const char*)(uintptr_t)(m_streams[0].OffsetInBytes + startV*s);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, 0, s, b);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, 1, s, b+16);
        if (s >= 28) { glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, 0, s, b+20); }
    }
}

STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT bv, UINT min, UINT num, UINT si, UINT pc) {
    g_drawCalls++; 
    float* f_ptr = NULL; if(m_streams[0].pStreamData) f_ptr = (float*)((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->m_pData;
    bool isRHW_flag = (f_ptr && (abs(f_ptr[0]) > 2.0f || abs(f_ptr[1]) > 2.0f));
    UpdateShaderProgram(isRHW_flag); ApplyAttributes(NULL, 0, bv);
    GLenum it = GL_UNSIGNED_SHORT; int is=2; if(m_pIndexData) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ((GLDirect3DIndexBuffer9*)m_pIndexData)->GetIBO()); if(((GLDirect3DIndexBuffer9*)m_pIndexData)->GetFormat()==D3DFMT_INDEX32) { it=GL_UNSIGNED_INT; is=4; } }
    glDrawElements(GetGLPrim(t), GetGLCount(t, pc), it, (void*)(uintptr_t)(si*is)); return D3D_OK;
}
STDMETHODIMP GLDirect3DDevice9::DrawPrimitive(D3DPRIMITIVETYPE t, UINT sv, UINT pc) {
    g_drawCalls++;
    float* f_ptr = NULL; if(m_streams[0].pStreamData) f_ptr = (float*)((GLDirect3DVertexBuffer9*)m_streams[0].pStreamData)->m_pData;
    bool isRHW_flag = (f_ptr && (abs(f_ptr[0]) > 2.0f || abs(f_ptr[1]) > 2.0f));
    UpdateShaderProgram(isRHW_flag); ApplyAttributes(NULL, 0, sv); glDrawArrays(GetGLPrim(t), 0, GetGLCount(t,pc)); return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::DrawPrimitiveUP(D3DPRIMITIVETYPE t, UINT pc, CONST void* d, UINT s) {
    g_drawCalls++;
    float* f_ptr = (float*)d; bool isRHW_flag = (f_ptr && (abs(f_ptr[0]) > 2.0f || abs(f_ptr[1]) > 2.0f));
    UpdateShaderProgram(isRHW_flag); ApplyAttributes(d, s, 0); glDrawArrays(GetGLPrim(t), 0, GetGLCount(t,pc)); return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t, UINT min, UINT num, UINT pc, CONST void* i, D3DFORMAT f_fmt, CONST void* d, UINT s) {
    g_drawCalls++;
    float* f_ptr = (float*)d; bool isRHW_flag = (f_ptr && (abs(f_ptr[0]) > 2.0f || abs(f_ptr[1]) > 2.0f));
    UpdateShaderProgram(isRHW_flag); ApplyAttributes(d, s, 0); glDrawElements(GetGLPrim(t), GetGLCount(t,pc), (f_fmt==D3DFMT_INDEX32?GL_UNSIGNED_INT:GL_UNSIGNED_SHORT), i); return D3D_OK; }

STDMETHODIMP GLDirect3DDevice9::ProcessVertices(UINT s, UINT d, UINT c, IDirect3DVertexBuffer9* db, IDirect3DVertexDeclaration9* vd, DWORD f) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateVertexDeclaration(CONST D3DVERTEXELEMENT9* e, IDirect3DVertexDeclaration9** p) { if(p) *p=new GLDirect3DVertexDeclaration9(e); return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::SetVertexDeclaration(IDirect3DVertexDeclaration9* p) { m_pVertexDecl=p; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetVertexDeclaration(IDirect3DVertexDeclaration9** p) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetFVF(DWORD f) { m_fvf=f; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetFVF(DWORD* p) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::CreateVertexShader(CONST DWORD* f, IDirect3DVertexShader9** p) { if(p) *p=new GLDirect3DVertexShader9(f); return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::SetVertexShader(IDirect3DVertexShader9* p) { m_pVertexShader=p; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetVertexShader(IDirect3DVertexShader9** p) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetVertexShaderConstantF(UINT s, CONST float* d, UINT c) { if(s+c<=256) memcpy(&m_vsConstF[s], d, c*16); return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetVertexShaderConstantF(UINT s, float* d, UINT c) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetVertexShaderConstantI(UINT s, CONST int* d, UINT c) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetVertexShaderConstantI(UINT s, int* d, UINT c) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetVertexShaderConstantB(UINT s, CONST BOOL* d, UINT c) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetVertexShaderConstantB(UINT s, BOOL* d, UINT c) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::CreatePixelShader(CONST DWORD* f, IDirect3DPixelShader9** p) { if(p) *p=new GLDirect3DPixelShader9(f); return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::SetPixelShader(IDirect3DPixelShader9* p) { m_pPixelShader=p; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetPixelShader(IDirect3DPixelShader9** p) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetPixelShaderConstantF(UINT s, CONST float* d, UINT c) { if(s+c<=256) memcpy(&m_psConstF[s], d, c*16); return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetPixelShaderConstantF(UINT s, float* d, UINT c) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetPixelShaderConstantI(UINT s, CONST int* d, UINT c) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetPixelShaderConstantI(UINT s, int* d, UINT c) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetPixelShaderConstantB(UINT s, CONST BOOL* d, UINT c) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetPixelShaderConstantB(UINT s, BOOL* d, UINT c) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::DrawRectPatch(UINT h, CONST float* n, CONST D3DRECTPATCH_INFO* p) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::DrawTriPatch(UINT h, CONST float* n, CONST D3DTRIPATCH_INFO* p) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::DeletePatch(UINT h) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::CreateQuery(D3DQUERYTYPE t, IDirect3DQuery9** p) { if(p) *p=new GLDirect3DQuery9(); return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::SetStreamSource(UINT n, IDirect3DVertexBuffer9* d, UINT o, UINT s) { if(n<16) { m_streams[n].pStreamData=d; m_streams[n].OffsetInBytes=o; m_streams[n].Stride=s; } return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetStreamSource(UINT n, IDirect3DVertexBuffer9** d, UINT* o, UINT* s) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetStreamSourceFreq(UINT n, UINT s) { return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetStreamSourceFreq(UINT n, UINT* s) { return E_NOTIMPL; }
STDMETHODIMP GLDirect3DDevice9::SetIndices(IDirect3DIndexBuffer9* d) { m_pIndexData=d; return D3D_OK; }
STDMETHODIMP GLDirect3DDevice9::GetIndices(IDirect3DIndexBuffer9** d) { return E_NOTIMPL; }

GLuint GLDirect3DDevice9::CompileShader(GLenum type, const char* source) {
    GLuint s = glCreateShader(type); glShaderSource(s, 1, &source, NULL); glCompileShader(s);
    GLint status; glGetShaderiv(s, GL_COMPILE_STATUS, &status);
    if (!status) { char info[512]; glGetShaderInfoLog(s, 512, NULL, info); fprintf(stderr, "Shader compile error (%s): %s\\n", (type == GL_VERTEX_SHADER ? "Vertex" : "Fragment"), info); }
    return s;
}

GLDirect3DVertexBuffer9::GLDirect3DVertexBuffer9(UINT len, DWORD u, DWORD fvf, D3DPOOL p) : m_length(len), m_pData(NULL), m_vbo(0) { m_pData=malloc(len); glGenBuffers(1,&m_vbo); }
GLDirect3DVertexBuffer9::~GLDirect3DVertexBuffer9() { if(m_pData) free(m_pData); if(m_vbo) glDeleteBuffers(1,&m_vbo); }
STDMETHODIMP GLDirect3DVertexBuffer9::Lock(UINT o, UINT s, void** p, DWORD f) { if(p) *p=(char*)m_pData+o; return D3D_OK; }
STDMETHODIMP GLDirect3DVertexBuffer9::Unlock() { glBindBuffer(GL_ARRAY_BUFFER, m_vbo); glBufferData(GL_ARRAY_BUFFER, m_length, m_pData, GL_DYNAMIC_DRAW); return D3D_OK; }
STDMETHODIMP GLDirect3DVertexBuffer9::GetDesc(D3DVERTEXBUFFER_DESC* p) { return E_NOTIMPL; }

GLDirect3DIndexBuffer9::GLDirect3DIndexBuffer9(UINT len, DWORD u, D3DFORMAT f, D3DPOOL p) : m_length(len), m_format(f), m_pData(NULL), m_ibo(0) { m_pData=malloc(len); glGenBuffers(1,&m_ibo); }
GLDirect3DIndexBuffer9::~GLDirect3DIndexBuffer9() { if(m_pData) free(m_pData); if(m_ibo) glDeleteBuffers(1,&m_ibo); }
STDMETHODIMP GLDirect3DIndexBuffer9::Lock(UINT o, UINT s, void** p, DWORD f) { if(p) *p=(char*)m_pData+o; return D3D_OK; }
STDMETHODIMP GLDirect3DIndexBuffer9::Unlock() { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_length, m_pData, GL_DYNAMIC_DRAW); return D3D_OK; }
STDMETHODIMP GLDirect3DIndexBuffer9::GetDesc(D3DINDEXBUFFER_DESC* p) { return E_NOTIMPL; }

GLDirect3DTexture9::GLDirect3DTexture9(UINT w, UINT h, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p) : m_tex(0), m_fbo(0), m_width(w), m_height(h), m_format(f), m_levels(l), m_pData(NULL) {
    m_pData=malloc(w*h*4); glGenTextures(1,&m_tex); glBindTexture(GL_TEXTURE_2D,m_tex); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
}
GLDirect3DTexture9::~GLDirect3DTexture9() { if(m_pData) free(m_pData); if(m_tex) glDeleteTextures(1,&m_tex); if(m_fbo) glDeleteFramebuffers(1,&m_fbo); }
STDMETHODIMP GLDirect3DTexture9::LockRect(UINT l, D3DLOCKED_RECT* p, CONST RECT* r, DWORD f) { if(p) { p->Pitch=m_width*4; p->pBits=m_pData; } return D3D_OK; }
STDMETHODIMP GLDirect3DTexture9::UnlockRect(UINT l) {
    glBindTexture(GL_TEXTURE_2D, m_tex);
    if(m_format==827611204) glCompressedTexImage2D(GL_TEXTURE_2D,l,0x83F3,m_width,m_height,0,(m_width/4)*(m_height/4)*8,m_pData);
    else if(m_format==894720068) glCompressedTexImage2D(GL_TEXTURE_2D,l,0x83F5,m_width,m_height,0,(m_width/4)*(m_height/4)*16,m_pData);
    else glTexImage2D(GL_TEXTURE_2D,l,GL_RGBA,m_width,m_height,0,GL_BGRA,GL_UNSIGNED_BYTE,m_pData);
    return D3D_OK;
}
STDMETHODIMP GLDirect3DTexture9::GetLevelDesc(UINT l, D3DSURFACE_DESC* p) { if(p) { p->Format=m_format; p->Width=m_width; p->Height=m_height; } return D3D_OK; }
STDMETHODIMP GLDirect3DTexture9::GetSurfaceLevel(UINT l, IDirect3DSurface9** ppS) { if(ppS) *ppS=new GLDirect3DSurface9(this,l); return D3D_OK; }
GLuint GLDirect3DTexture9::GetFBO() { 
    if (!m_fbo) { 
        glGenFramebuffers(1, &m_fbo); 
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    return m_fbo;
}

GLDirect3DSurface9::GLDirect3DSurface9(GLDirect3DTexture9* p, UINT l) : m_pParent(p), m_level(l) {}
STDMETHODIMP GLDirect3DSurface9::GetDesc(D3DSURFACE_DESC *p) { if(m_pParent) return m_pParent->GetLevelDesc(m_level, p); return D3D_OK; }
STDMETHODIMP GLDirect3DSurface9::LockRect(D3DLOCKED_RECT* p, CONST RECT* r, DWORD f) { if(m_pParent) return m_pParent->LockRect(m_level,p,r,f); return E_NOTIMPL; }
STDMETHODIMP GLDirect3DSurface9::UnlockRect() { if(m_pParent) return m_pParent->UnlockRect(m_level); return D3D_OK; }

GLDirect3DVertexDeclaration9::GLDirect3DVertexDeclaration9(CONST D3DVERTEXELEMENT9* e) : m_refCount(1) { while(e->Stream!=0xFF) { m_elements.push_back(*e); e++; } }
STDMETHODIMP GLDirect3DVertexDeclaration9::GetDeclaration(D3DVERTEXELEMENT9* e, UINT* n) { if(n) *n=m_elements.size(); if(e) for(int i=0;i<(int)m_elements.size();i++) e[i]=m_elements[i]; return D3D_OK; }
GLDirect3DVertexShader9::GLDirect3DVertexShader9(CONST DWORD* f) : m_refCount(1) { if(f) { int s=0; while(f[s]!=0x0000FFFF) s++; s++; m_function.assign(f,f+s); } }
STDMETHODIMP GLDirect3DVertexShader9::GetFunction(void* d, UINT* s) { if(s) *s=m_function.size()*4; if(d) memcpy(d,&m_function[0],m_function.size()*4); return D3D_OK; }
GLDirect3DPixelShader9::GLDirect3DPixelShader9(CONST DWORD* f) : m_refCount(1) { if(f) { int s=0; while(f[s]!=0x0000FFFF) s++; s++; m_function.assign(f,f+s); } }
STDMETHODIMP GLDirect3DPixelShader9::GetFunction(void* d, UINT* s) { if(s) *s=m_function.size()*4; if(d) memcpy(d,&m_function[0],m_function.size()*4); return D3D_OK; }

void GLDirect3DDevice9::SetSDLWindow(void* w) { g_sdlWindow = w; }
"""

with open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w") as f:
    f.write(content)
