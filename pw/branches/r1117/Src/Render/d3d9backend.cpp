#include "stdafx.h"
#include "d3d9backend.h"
#include "renderflagsconverter.h"
#include "dxutils.h"   // GetD3DPoolAndUsagesParamaters + PoolType (D3D9-реализация знает про D3D9)

namespace Render
{


//////////////////////////////////////////////////////////////////////////
// Переносится КАК ЕСТЬ: вызовы соответствуют прежним местам в renderer.cpp
// (см. docs/PLAN_NVRHI.md §Этап 2 — «D3D9Backend — перенос
// SmartRenderer/Renderer как есть»). Логика здесь не меняется: иначе пропадает
// смысл сравнения картинки с D3D9-эталоном.

HRESULT D3D9Backend::BeginScene() {
  return pDevice->BeginScene();
}

HRESULT D3D9Backend::EndScene() {
  return pDevice->EndScene();
}

// BackendRect -> RECT: Win32-структура живёт только внутри D3D9-реализации
// (seam обязан пережить не-Windows, см. renderbackend.h).
static bool ToD3DRect(const BackendRect* pIn, RECT& out) {
  if (!pIn)
    return false;
  out.left = pIn->left; out.top = pIn->top; out.right = pIn->right; out.bottom = pIn->bottom;
  return true;
}

HRESULT D3D9Backend::Present(NativeWindow window, const BackendRect* pSrcRect, const BackendRect* pDstRect) {
  // Единственное место, где ручка seam'а становится HWND (окно — граница
  // платформы, см. renderwindow.h).
  RECT srcRect, dstRect;
  return pDevice->Present(ToD3DRect(pSrcRect, srcRect) ? &srcRect : NULL,
                          ToD3DRect(pDstRect, dstRect) ? &dstRect : NULL,
                          static_cast<HWND>(window.p), NULL);
}

void D3D9Backend::GetViewport(BackendViewport& vp) {
  D3DVIEWPORT9 d3dvp;
  pDevice->GetViewport(&d3dvp);
  vp.x = d3dvp.X;
  vp.y = d3dvp.Y;
  vp.width = d3dvp.Width;
  vp.height = d3dvp.Height;
  vp.minZ = d3dvp.MinZ;
  vp.maxZ = d3dvp.MaxZ;
}

void D3D9Backend::SetScissorRect(const BackendRect* pRect) {
  RECT rect;
  pDevice->SetScissorRect(ToD3DRect(pRect, rect) ? &rect : NULL);
}

void D3D9Backend::GetScissorRect(BackendRect* pOut) {
  if (!pOut)
    return;
  RECT rect = { 0, 0, 0, 0 };
  pDevice->GetScissorRect(&rect);
  *pOut = MakeBackendRect(rect.left, rect.top, rect.right, rect.bottom);
}

HRESULT D3D9Backend::Clear(unsigned clearFlags, unsigned colorRGBA, float z, unsigned stencil) {
  DWORD d3dFlags = 0;
  if (clearFlags & bcTarget)
    d3dFlags |= D3DCLEAR_TARGET;
  if (clearFlags & bcZBuffer)
    d3dFlags |= D3DCLEAR_ZBUFFER;
  if (clearFlags & bcStencil)
    d3dFlags |= D3DCLEAR_STENCIL;
  return pDevice->Clear(0, 0, d3dFlags, colorRGBA, z, stencil);
}

HRESULT D3D9Backend::SetVertexShaderConstants(unsigned startRegister, unsigned registerCount, const void* pData) {
  return pDevice->SetVertexShaderConstantF(startRegister, reinterpret_cast<CONST float*>(pData), registerCount);
}

HRESULT D3D9Backend::SetPixelShaderConstants(unsigned startRegister, unsigned registerCount, const void* pData) {
  return pDevice->SetPixelShaderConstantF(startRegister, static_cast<const float*>(pData), registerCount);
}

HRESULT D3D9Backend::SetStreamSource(unsigned slot, HBuffer vertexBuffer, unsigned byteOffset, unsigned stride) {
  return pDevice->SetStreamSource(slot, static_cast<IDirect3DVertexBuffer9*>(vertexBuffer.p), byteOffset, stride);
}

HRESULT D3D9Backend::SetIndices(HBuffer indexBuffer) {
  return pDevice->SetIndices(static_cast<IDirect3DIndexBuffer9*>(indexBuffer.p));
}

HRESULT D3D9Backend::SetInputLayout(HInputLayout layout) {
  return pDevice->SetVertexDeclaration(static_cast<IDirect3DVertexDeclaration9*>(layout.p));
}

HRESULT D3D9Backend::SetVertexShader(HShader shader) {
  return pDevice->SetVertexShader(static_cast<IDirect3DVertexShader9*>(shader.p));
}

HRESULT D3D9Backend::SetPixelShader(HShader shader) {
  return pDevice->SetPixelShader(static_cast<IDirect3DPixelShader9*>(shader.p));
}

HRESULT D3D9Backend::SetTexture(unsigned samplerIndex, HTexture texture) {
  return pDevice->SetTexture(samplerIndex, static_cast<IDirect3DBaseTexture9*>(texture.p));
}

HRESULT D3D9Backend::SetRenderTarget(unsigned index, HSurface surface) {
  return pDevice->SetRenderTarget(index, static_cast<IDirect3DSurface9*>(surface.p));
}

HRESULT D3D9Backend::SetDepthStencil(HSurface surface) {
  // IDirect3DDepthStencilSurface9 в этом билде нет: d3d9.h из Vendor/DirectX
  // объявляет SetDepthStencilSurface(IDirect3DSurface9*).
  return pDevice->SetDepthStencilSurface(static_cast<IDirect3DSurface9*>(surface.p));
}

HRESULT D3D9Backend::SetViewport(const BackendViewport& vp) {
  D3DVIEWPORT9 d3dvp;
  d3dvp.X = vp.x;
  d3dvp.Y = vp.y;
  d3dvp.Width = vp.width;
  d3dvp.Height = vp.height;
  d3dvp.MinZ = vp.minZ;
  d3dvp.MaxZ = vp.maxZ;
  return pDevice->SetViewport(&d3dvp);
}

HRESULT D3D9Backend::DrawIndexed(ERenderPrimitiveType type, unsigned baseVertexIndex, unsigned minIndex,
                                 unsigned indexedVertexCount, unsigned startIndex, unsigned primitiveCount) {
  ApplyInstanceFreqs();
  return pDevice->DrawIndexedPrimitive(ConvertPrimitiveType(type), baseVertexIndex, minIndex,
                                       indexedVertexCount, startIndex, primitiveCount);
}

HRESULT D3D9Backend::Draw(ERenderPrimitiveType type, unsigned startVertex, unsigned primitiveCount) {
  ApplyInstanceFreqs();
  return pDevice->DrawPrimitive(ConvertPrimitiveType(type), startVertex, primitiveCount);
}

HRESULT D3D9Backend::DrawIndexedUP(ERenderPrimitiveType type, unsigned minIndexValue, unsigned indexCount,
                                   unsigned vertexCount, const void* pIndexData, bool index16,
                                   const void* pVertexData, unsigned vertexStride) {
  ApplyInstanceFreqs();
  return pDevice->DrawIndexedPrimitiveUP(ConvertPrimitiveType(type), minIndexValue, indexCount, vertexCount,
                                         pIndexData, index16 ? D3DFMT_INDEX16 : D3DFMT_INDEX32,
                                         pVertexData, vertexStride);
}

HRESULT D3D9Backend::DrawUP(ERenderPrimitiveType type, unsigned primitiveCount,
                            const void* pVertexData, unsigned vertexStride) {
  ApplyInstanceFreqs();
  return pDevice->DrawPrimitiveUP(ConvertPrimitiveType(type), primitiveCount, pVertexData, vertexStride);
}

HRESULT D3D9Backend::CreateVertexShader(const void* pShaderCode, unsigned codeSize, HShader* pOut) {
  (void)codeSize;   // D3D9 размер не спрашивает: DXBC описывает свой размер сам
  IDirect3DVertexShader9* shader = 0;
  HRESULT hr = pDevice->CreateVertexShader(static_cast<const DWORD*>(pShaderCode), &shader);
  *pOut = MakeBackendHandle<HShader>(shader);
  return hr;
}

HRESULT D3D9Backend::CreatePixelShader(const void* pShaderCode, unsigned codeSize, HShader* pOut) {
  (void)codeSize;
  IDirect3DPixelShader9* shader = 0;
  HRESULT hr = pDevice->CreatePixelShader(static_cast<const DWORD*>(pShaderCode), &shader);
  *pOut = MakeBackendHandle<HShader>(shader);
  return hr;
}

// Перенос перевода VertexFormatDescriptor -> D3DVERTEXELEMENT9 из
// Renderer::CreateVertexFormatDeclaration: этот перевод и есть часть бэкенда
// (в Vulkan на его месте VkVertexInputState).
HRESULT D3D9Backend::CreateInputLayout(const VertexFormatDescriptor& descr, HInputLayout* pOut) {
  unsigned int elements_count = descr.GetVertexElementsCount();

  nstl::vector<D3DVERTEXELEMENT9> elements;
  elements.reserve(elements_count);

  for (unsigned int i = 0; i < elements_count; i++)
  {
    const VertexElementDescriptor& element = descr.GetVertexElement(i);

    D3DVERTEXELEMENT9 d3d_element;
    d3d_element.Stream       = static_cast<unsigned short>(element.stream);
    d3d_element.Offset       = static_cast<unsigned short>(element.offset);
    d3d_element.Type         = ConvertVertexElementType(element.type);
    d3d_element.Method       = D3DDECLMETHOD_DEFAULT;
    d3d_element.Usage        = ConvertVertexElementUsage(element.usage);
    d3d_element.UsageIndex   = static_cast<unsigned char>(element.usageIndex);

    elements.push_back(d3d_element);
  }
  D3DVERTEXELEMENT9 d3d_decl_end = D3DDECL_END();
  elements.push_back(d3d_decl_end);

  IDirect3DVertexDeclaration9* decl_d3d = 0;
  HRESULT hr = pDevice->CreateVertexDeclaration(&elements[0], &decl_d3d);
  *pOut = MakeBackendHandle<HInputLayout>(decl_d3d);
  return hr;
}

// Нейтральный пул -> D3DPOOL + usage. Единственное место, где EBackendPool
// превращается в D3D9-параметры (docs/PLAN_NVRHI.md §2.3): в Vulkan/DX12 тот же
// enum разворачивается в staging + default + upload-кольцо.
static void D3DPoolFromBackend(EBackendPool pool, DWORD& usage, D3DPOOL& d3dPool) {
  // Паритет словарей пулов: PoolType (dxutils.h) — словарь вызывающего кода,
  // EBackendPool (renderbackend.h) — словарь seam'а; static_cast ниже требует,
  // чтобы значения совпадали по порядку. NI_STATIC_ASSERT разворачивается в
  // блок — поэтому он внутри функции, а не в файловом скоупе.
  NI_STATIC_ASSERT((int)RENDER_POOL_DEFAULT        == (int)BACKEND_POOL_DEFAULT,        pool_default_drift);
  NI_STATIC_ASSERT((int)RENDER_POOL_MANAGED        == (int)BACKEND_POOL_MANAGED,        pool_managed_drift);
  NI_STATIC_ASSERT((int)RENDER_POOL_DYNAMIC        == (int)BACKEND_POOL_DYNAMIC,        pool_dynamic_drift);
  NI_STATIC_ASSERT((int)RENDER_POOL_TEX_DYNAMIC    == (int)BACKEND_POOL_TEX_DYNAMIC,    pool_tex_dynamic_drift);
  NI_STATIC_ASSERT((int)RENDER_POOL_SYSMEM         == (int)BACKEND_POOL_SYSMEM,         pool_sysmem_drift);
  NI_STATIC_ASSERT((int)RENDER_POOL_SYSMEM_DYNAMIC == (int)BACKEND_POOL_SYSMEM_DYNAMIC, pool_sysmem_dynamic_drift);

  NI_ASSERT(pool >= BACKEND_POOL_DEFAULT && pool < BACKEND_POOL_COUNT, "bad backend pool");
  GetD3DPoolAndUsagesParamaters(usage, d3dPool, static_cast<PoolType>(pool));
}

// EBackendFormat -> D3DFORMAT. bfUnknown — отказ (D3DFMT_UNKNOWN): подменять
// неизвестный формат каким-нибудь рабочим нельзя, молча поехавший формат/альфа
// в логах не виден.
static D3DFORMAT D3DFormatFromBackend(EBackendFormat format) {
  switch (format) {
    case bfA8R8G8B8:       return D3DFMT_A8R8G8B8;
    case bfX8R8G8B8:       return D3DFMT_X8R8G8B8;
    case bfA8:             return D3DFMT_A8;
    case bfL8:             return D3DFMT_L8;
    case bfL16:            return D3DFMT_L16;
    case bfR5G6B5:         return D3DFMT_R5G6B5;
    case bfR16F:           return D3DFMT_R16F;
    case bfA16B16G16R16F:  return D3DFMT_A16B16G16R16F;
    case bfR32F:           return D3DFMT_R32F;
    case bfG32R32F:        return D3DFMT_G32R32F;
    case bfA32B32G32R32F:  return D3DFMT_A32B32G32R32F;
    case bfDXT1:           return D3DFMT_DXT1;
    case bfDXT2:           return D3DFMT_DXT2;
    case bfDXT3:           return D3DFMT_DXT3;
    case bfDXT4:           return D3DFMT_DXT4;
    case bfDXT5:           return D3DFMT_DXT5;
    case bfD24S8:          return D3DFMT_D24S8;
    default:               return D3DFMT_UNKNOWN;
  }
}

static DWORD D3DUsageFromBackend(EBackendUsage usage) {
  DWORD d3dUsage = 0;
  if (usage & usageRenderTarget)  d3dUsage |= D3DUSAGE_RENDERTARGET;
  if (usage & usageDepthStencil)  d3dUsage |= D3DUSAGE_DEPTHSTENCIL;
  if (usage & usageAutoGenMipmap) d3dUsage |= D3DUSAGE_AUTOGENMIPMAP;
  return d3dUsage;
}

// Пул + «назначение» -> D3DPOOL + D3DUSAGE_*: пуловая часть берётся из
// D3DPoolFromBackend (он же проверяет паритет словарей пулов), биты назначения
// добавляются сверху.
static void D3DResPoolUsageFromBackend(EBackendPool pool, EBackendUsage usage,
                                       DWORD& d3dUsage, D3DPOOL& d3dPool) {
  D3DPoolFromBackend(pool, d3dUsage, d3dPool);
  d3dUsage |= D3DUsageFromBackend(usage);
}

HRESULT D3D9Backend::CreateTexture2D(unsigned width, unsigned height, unsigned mipLevels,
                                     EBackendUsage usage, EBackendFormat format, EBackendPool pool,
                                     HTexture* pOut) {
  if (!pOut)
    return E_INVALIDARG;
  const D3DFORMAT fmt = D3DFormatFromBackend(format);
  if (fmt == D3DFMT_UNKNOWN)
    return D3DERR_INVALIDCALL;
  DWORD d3dUsage = 0;
  D3DPOOL d3dPool = D3DPOOL_MANAGED;
  D3DResPoolUsageFromBackend(pool, usage, d3dUsage, d3dPool);

  IDirect3DTexture9* tex = 0;
  const HRESULT hr = pDevice->CreateTexture(width, height, mipLevels, d3dUsage, fmt, d3dPool, &tex, 0);
  *pOut = MakeBackendHandle<HTexture>(tex);
  return hr;
}

HRESULT D3D9Backend::CreateTextureCube(unsigned edgeLen, unsigned mipLevels,
                                       EBackendUsage usage, EBackendFormat format, EBackendPool pool,
                                       HTexture* pOut) {
  if (!pOut)
    return E_INVALIDARG;
  const D3DFORMAT fmt = D3DFormatFromBackend(format);
  if (fmt == D3DFMT_UNKNOWN)
    return D3DERR_INVALIDCALL;
  DWORD d3dUsage = 0;
  D3DPOOL d3dPool = D3DPOOL_MANAGED;
  D3DResPoolUsageFromBackend(pool, usage, d3dUsage, d3dPool);

  IDirect3DCubeTexture9* cube = 0;
  const HRESULT hr = pDevice->CreateCubeTexture(edgeLen, mipLevels, d3dUsage, fmt, d3dPool, &cube, 0);
  *pOut = MakeBackendHandle<HTexture>(cube);
  return hr;
}

HRESULT D3D9Backend::CreateSurface(EBackendSurfaceKind kind, unsigned width, unsigned height,
                                   EBackendFormat format, EBackendPool pool, HSurface* pOut) {
  if (!pOut)
    return E_INVALIDARG;
  const D3DFORMAT fmt = D3DFormatFromBackend(format);
  if (fmt == D3DFMT_UNKNOWN)
    return D3DERR_INVALIDCALL;

  IDirect3DSurface9* surf = 0;
  HRESULT hr;
  // MSAA в клиенте не используется (замер: D3DMULTISAMPLE_NONE, качество 0),
  // поэтому в сигнатуре seam'а его нет.
  switch (kind) {
    case ssDepthStencil:
      hr = pDevice->CreateDepthStencilSurface(width, height, fmt, D3DMULTISAMPLE_NONE, 0, FALSE, &surf, 0);
      break;
    case ssRenderTarget:
      hr = pDevice->CreateRenderTarget(width, height, fmt, D3DMULTISAMPLE_NONE, 0, FALSE, &surf, 0);
      break;
    default: {
      DWORD d3dUsage = 0;
      D3DPOOL d3dPool = D3DPOOL_SYSTEMMEM;
      D3DPoolFromBackend(pool, d3dUsage, d3dPool);
      hr = pDevice->CreateOffscreenPlainSurface(width, height, fmt, d3dPool, &surf, 0);
      break;
    }
  }
  *pOut = MakeBackendHandle<HSurface>(surf);
  return hr;
}

HRESULT D3D9Backend::UpdateTexture(HTexture srcTexture, HTexture dstTexture) {
  return pDevice->UpdateTexture(static_cast<IDirect3DBaseTexture9*>(srcTexture.p),
                                static_cast<IDirect3DBaseTexture9*>(dstTexture.p));
}

HRESULT D3D9Backend::CreateVertexBuffer(unsigned size, EBackendPool pool, HBuffer* pOut) {
  DWORD usage;
  D3DPOOL d3dPool;
  D3DPoolFromBackend(pool, usage, d3dPool);

  IDirect3DVertexBuffer9* vb = 0;
  HRESULT hr = pDevice->CreateVertexBuffer(size, usage, 0, d3dPool, &vb, 0);
  *pOut = MakeBackendHandle<HBuffer>(vb);
  return hr;
}

HRESULT D3D9Backend::CreateIndexBuffer(unsigned size, bool index32, EBackendPool pool, HBuffer* pOut) {
  DWORD usage;
  D3DPOOL d3dPool;
  D3DPoolFromBackend(pool, usage, d3dPool);

  IDirect3DIndexBuffer9* ib = 0;
  HRESULT hr = pDevice->CreateIndexBuffer(size, usage,
                                          index32 ? D3DFMT_INDEX32 : D3DFMT_INDEX16,
                                          d3dPool, &ib, 0);
  *pOut = MakeBackendHandle<HBuffer>(ib);
  return hr;
}

// Режим блокировки -> флаги D3D9: общая таблица ConvertRenderLockType()
// (renderflagsconverter.cpp), та же, что для поверхностей.
HRESULT D3D9Backend::LockVertexBuffer(HBuffer vb, unsigned offset, unsigned size,
                                      ERenderLockType mode, void** ppData) {
  if (!vb.p || !ppData) return D3DERR_INVALIDCALL;
  return static_cast<IDirect3DVertexBuffer9*>(vb.p)->Lock(offset, size, ppData,
                                                          ConvertRenderLockType(mode));
}

HRESULT D3D9Backend::UnlockVertexBuffer(HBuffer vb) {
  if (!vb.p) return D3DERR_INVALIDCALL;
  return static_cast<IDirect3DVertexBuffer9*>(vb.p)->Unlock();
}

HRESULT D3D9Backend::LockIndexBuffer(HBuffer ib, unsigned offset, unsigned size,
                                     ERenderLockType mode, void** ppData) {
  if (!ib.p || !ppData) return D3DERR_INVALIDCALL;
  return static_cast<IDirect3DIndexBuffer9*>(ib.p)->Lock(offset, size, ppData,
                                                         ConvertRenderLockType(mode));
}

HRESULT D3D9Backend::UnlockIndexBuffer(HBuffer ib) {
  if (!ib.p) return D3DERR_INVALIDCALL;
  return static_cast<IDirect3DIndexBuffer9*>(ib.p)->Unlock();
}

HRESULT D3D9Backend::GetRenderTargetData(HSurface srcSurface, HSurface dstSurface) {
  return pDevice->GetRenderTargetData(static_cast<IDirect3DSurface9*>(srcSurface.p),
                                      static_cast<IDirect3DSurface9*>(dstSurface.p));
}

HRESULT D3D9Backend::ColorFill(HSurface surface, unsigned colorRGBA) {
  return pDevice->ColorFill(static_cast<IDirect3DSurface9*>(surface.p), NULL, colorRGBA);
}

HRESULT D3D9Backend::StretchRect(HSurface src, const BackendRect* pSrcRect, HSurface dst,
                                 const BackendRect* pDstRect, EBackendFilter filter) {
  RECT srcRect, dstRect;
  return pDevice->StretchRect(static_cast<IDirect3DSurface9*>(src.p),
                              ToD3DRect(pSrcRect, srcRect) ? &srcRect : 0,
                              static_cast<IDirect3DSurface9*>(dst.p),
                              ToD3DRect(pDstRect, dstRect) ? &dstRect : 0,
                              filter == bfLinear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
}

HRESULT D3D9Backend::ReadSurface(HSurface srcSurface, const BackendRect* pCropRect, void* pDstBuffer,
                                 unsigned dstRowPitch, EBackendPixelLayout* pOutLayout) {
  IDirect3DSurface9* pSrc = static_cast<IDirect3DSurface9*>(srcSurface.p);
  if (!pSrc || !pDstBuffer || !dstRowPitch)
    return E_INVALIDARG;

  D3DSURFACE_DESC desc;
  HRESULT hr = pSrc->GetDesc(&desc);
  if (FAILED(hr))
    return hr;
  // Readback в клиенте нужен только снимку кадра, а кадровая поверхность и
  // бэк-буфер — 32 бита (A8R8G8B8/X8R8G8B8). Другой формат — отказ, а не
  // тихая подмена: молча переписать каналы/шаг строки хуже ошибки.
  if (desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_X8R8G8B8)
    return D3DERR_INVALIDCALL;

  // Поверхности пула DEFAULT (RT, бэк-буфер) процессору недоступны — сначала
  // копируем в системную память (D3D9 требует, чтобы источник был RT или
  // бэк-буфером; для остальных DEFAULT это просто не сработает).
  IDirect3DSurface9* pRead = pSrc;
  IDirect3DSurface9* pStaging = 0;
  if (desc.Pool == D3DPOOL_DEFAULT) {
    if (FAILED(pDevice->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
                                                    D3DPOOL_SYSTEMMEM, &pStaging, NULL)))
      return D3DERR_INVALIDCALL;
    hr = pDevice->GetRenderTargetData(pSrc, pStaging);
    if (FAILED(hr)) {
      pStaging->Release();
      return hr;
    }
    pRead = pStaging;
  }

  const unsigned x0 = pCropRect ? (unsigned)pCropRect->left   : 0;
  const unsigned y0 = pCropRect ? (unsigned)pCropRect->top    : 0;
  const unsigned w  = pCropRect ? (unsigned)(pCropRect->right  - pCropRect->left) : desc.Width;
  const unsigned h  = pCropRect ? (unsigned)(pCropRect->bottom - pCropRect->top)  : desc.Height;
  if (x0 + w > desc.Width || y0 + h > desc.Height) {
    if (pStaging) pStaging->Release();
    return D3DERR_INVALIDCALL;
  }

  D3DLOCKED_RECT lr;
  hr = pRead->LockRect(&lr, NULL, D3DLOCK_READONLY);
  if (FAILED(hr)) {
    if (pStaging) pStaging->Release();
    return hr;
  }
  const unsigned char* pSrcBytes = static_cast<const unsigned char*>(lr.pBits);
  unsigned char* pDst = static_cast<unsigned char*>(pDstBuffer);
  for (unsigned row = 0; row < h; ++row)
    memcpy(pDst + (size_t)row * dstRowPitch,
           pSrcBytes + (size_t)(y0 + row) * lr.Pitch + (size_t)x0 * 4,
           (size_t)w * 4);
  pRead->UnlockRect();
  if (pStaging) pStaging->Release();

  if (pOutLayout)
    *pOutLayout = plBGRA;   // A8R8G8B8/X8R8G8B8 в памяти = B,G,R,A
  return S_OK;
}

HRESULT D3D9Backend::GetRenderTarget(unsigned index, HSurface* pOut) {
  if (!pOut)
    return E_INVALIDARG;
  IDirect3DSurface9* pSurf = 0;
  const HRESULT hr = pDevice->GetRenderTarget(index, &pSurf);
  if (SUCCEEDED(hr))
    pOut->p = pSurf;   // владеющая: GetRenderTarget() добавил ссылку, её снимает вызывающий
  return hr;
}

HRESULT D3D9Backend::GetDepthStencil(HSurface* pOut) {
  if (!pOut)
    return E_INVALIDARG;
  IDirect3DSurface9* pSurf = 0;
  const HRESULT hr = pDevice->GetDepthStencilSurface(&pSurf);
  if (SUCCEEDED(hr))
    pOut->p = pSurf;   // владеющая
  return hr;
}

HRESULT D3D9Backend::GetBackBuffer(HSurface* pOut) {
  if (!pOut)
    return E_INVALIDARG;
  IDirect3DSurface9* pBB = 0;
  const HRESULT hr = pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBB);
  if (SUCCEEDED(hr)) {
    pOut->p = pBB;
    pBB->Release();   // владеет устройством; ручка действительна до конца вызова звенящего
  }
  return hr;
}

void D3D9Backend::SetClipPlane(bool enable, const float* pPlaneEquation) {
  // В D3D9 это два вызова: разрешение слота и запись уравнения. Пара держится
  // здесь, чтобы вызывающий код не знал про D3DRS_CLIPPLANEENABLE.
  if (enable) {
    pDevice->SetRenderState(D3DRS_CLIPPLANEENABLE, 1);
    pDevice->SetClipPlane(0, pPlaneEquation);
  } else {
    pDevice->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
  }
}

void D3D9Backend::EvictManagedResources() {
  pDevice->EvictManagedResources();
}

//////////////////////////////////////////////////////////////////////////
// Инстансинг (этап 2 плана, docs/PLAN_NVRHI.md §2.10). Механизм D3D9 целиком
// здесь: раньше SmartRenderer держал массивы запрошенных/выставленных частот и
// сам звал SetStreamSourceFreq в ApplyStates() перед каждым рисованием.

HRESULT D3D9Backend::SetInstanceCount(unsigned count) {
  // Число экземпляров кодируется в старших битах DWORD (младший занят под
  // INSTANCEDATA/INDEXEDDATA) — прежняя проверка жила в EnableHardwareInstancing.
  if (count >= D3DSTREAMSOURCE_INDEXEDDATA)
    return D3DERR_INVALIDCALL;
  instanceCount = count;
  return D3D_OK;
}

// Частоты применяются дельтой и только перед рисованием: биндинги потоков к
// этому моменту уже выполнены (исторический порядок ApplyStates — внутри
// Draw*Primitive после биндингов), и переставлять его нечем оправдывать.
void D3D9Backend::ApplyInstanceFreqs() {
  unsigned requested[kFreqStreams];
  for (unsigned n = 0; n < kFreqStreams; ++n)
    requested[n] = 1;

  if (instanceCount) {
    // Поток 0 — геометрия: проходится заново на каждый экземпляр.
    requested[0] = D3DSTREAMSOURCE_INDEXEDDATA | instanceCount;
    // Поток 1 — данные экземпляра: одно значение на экземпляр.
    requested[1] = D3DSTREAMSOURCE_INSTANCEDATA | 1;
  }

  for (unsigned n = 0; n < kFreqStreams; ++n)
    if (streamFreq[n] != requested[n])
      pDevice->SetStreamSourceFreq(n, streamFreq[n] = requested[n]);
}

HRESULT D3D9Backend::CreateInstancedInputLayout(HInputLayout base, HInputLayout* pOut) {
  *pOut = HInputLayout();
  IDirect3DVertexDeclaration9* pBase = static_cast<IDirect3DVertexDeclaration9*>(base.p);
  if (!pBase)
    return D3DERR_INVALIDCALL;

  D3DVERTEXELEMENT9 decl[MAXD3DDECLLENGTH];
  UINT numElements = 0;
  HRESULT hr = pBase->GetDeclaration(decl, &numElements);
  if (FAILED(hr))
    return hr;
  // Последний элемент — D3DDECL_END(); плюс новый элемент перед ним.
  if (numElements < 2 || numElements >= MAXD3DDECLLENGTH)
    return D3DERR_INVALIDCALL;

  for (UINT n = 0; n + 1 < numElements; ++n)
    if (decl[n].Stream)
      ++decl[n].Stream;

  static const D3DVERTEXELEMENT9 instanceElement = { 1, 0, D3DDECLTYPE_SHORT2, 0,
                                                     D3DDECLUSAGE_BLENDINDICES, 0 };
  static const D3DVERTEXELEMENT9 declEnd = D3DDECL_END();

  decl[numElements - 1] = instanceElement;
  decl[numElements] = declEnd;

  IDirect3DVertexDeclaration9* pNew = 0;
  hr = pDevice->CreateVertexDeclaration(decl, &pNew);
  if (SUCCEEDED(hr))
    *pOut = MakeBackendHandle<HInputLayout>(pNew);
  return hr;
}

void D3D9Backend::InvalidateBoundState() {
  instanceCount = 0;
  for (unsigned n = 0; n < kFreqStreams; ++n)
    streamFreq[n] = 1;
}
} // namespace Render
