#include "stdafx.h"
#include "d3d9backend.h"
#include "renderflagsconverter.h"
#include "dxutils.h"   // GetD3DPoolAndUsagesParamaters + PoolType (D3D9-реализация знает про D3D9)
#include "renderstatesmanager.h"  // VerifyShadowAgainstDevice (проверка полноты модели)
#include "System/Compatibility.h"   // IsRunnedUnderWine / IsRunnedUnderCrossOverWine (mip-цепочка)

namespace Render
{


//////////////////////////////////////////////////////////////////////////
// Сведения об адаптере (шаг 4, docs/PLAN_NVRHI.md §2.5). Это МЕСТО, где D3D9
// перестаёт быть источником caps для клиентского кода: D3DCAPS9 +
// GetAdapterIdentifier здесь превращаются в нейтральный BackendSystemInfo,
// а NvrhiBackend на этапе 3 обязан выдать тот же контракт (эталон пинов
// матчера снят заранее — §2.5).

HRESULT D3D9Backend::GetSystemInfo(BackendSystemInfo& out) {
  D3DCAPS9 caps9;
  ZeroMemory(&caps9, sizeof(caps9));
  HRESULT hr = pDevice->GetDeviceCaps(&caps9);
  if (FAILED(hr))
    return hr;

  out.maxVertexShaderConst = caps9.MaxVertexShaderConst;
  out.isNullRef = (D3DDEVTYPE_NULLREF == caps9.DeviceType);

  IDirect3D9* pRawD3D = NULL;
  hr = pDevice->GetDirect3D(&pRawD3D);
  if (FAILED(hr) || !pRawD3D)
    return hr;
  IntrusivePtr<IDirect3D9> pD3D;
  pD3D.Attach(pRawD3D);

  D3DADAPTER_IDENTIFIER9 id;
  ZeroMemory(&id, sizeof(id));
  hr = pD3D->GetAdapterIdentifier(caps9.AdapterOrdinal, 0, &id);
  if (FAILED(hr))
    return hr;

  out.vendorId      = id.VendorId;
  out.deviceId      = id.DeviceId;
  out.driverVersion = (unsigned long long)id.DriverVersion.QuadPart;
  // Описание — в собственный буфер: id живёт на стеке вызова.
  lstrcpynA(adapterDescription, id.Description, (int)sizeof(adapterDescription));
  out.description = adapterDescription;

  // Depth-текстуры (гейт аппаратных карт теней). Прежний запрос из
  // ConfigManager::CheckFormats переехал сюда без изменений — иначе на стенде
  // (базовый драйвер) caps остались бы другими и матчер config.txt разрешил бы
  // другой список пинов.
  out.features = 0;
  D3DDISPLAYMODE dm;
  if (SUCCEEDED(pD3D->GetAdapterDisplayMode(caps9.AdapterOrdinal, &dm))) {
#define HAS_DEPTH_TEXTURE(fmt) \
    (D3D_OK == pD3D->CheckDeviceFormat(caps9.AdapterOrdinal, caps9.DeviceType, dm.Format, \
                                       D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, fmt))
    if (HAS_DEPTH_TEXTURE(D3DFMT_D16))   out.features |= hasD16T;
    if (HAS_DEPTH_TEXTURE(D3DFMT_D24X8)) out.features |= hasD24X;
    if (HAS_DEPTH_TEXTURE(D3DFMT_D24S8)) out.features |= hasD24S;
#undef HAS_DEPTH_TEXTURE
  }

  return S_OK;
}


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

HRESULT D3D9Backend::GetShaderConstants(EShaderConstStage stage, unsigned startRegister,
                                        unsigned registerCount, void* pData) {
  if (!pData || !registerCount || startRegister + registerCount > (unsigned)SHADER_CONST_REGISTERS)
    return D3DERR_INVALIDCALL;
  // Никогда не писавшийся регистр D3D9 отдаёт нулями — ровно так же, как
  // пустая тень, поэтому сравнение не даёт ложных расхождений на «мусоре».
  return stage == csVertex
    ? pDevice->GetVertexShaderConstantF(startRegister, static_cast<float*>(pData), registerCount)
    : pDevice->GetPixelShaderConstantF(startRegister, static_cast<float*>(pData), registerCount);
}

HRESULT D3D9Backend::SetStreamSource(unsigned slot, HBuffer vertexBuffer, unsigned byteOffset, unsigned stride) {
  return SetStreamSlot(MapStream(slot), static_cast<IDirect3DVertexBuffer9*>(vertexBuffer.p),
                       byteOffset, stride);
}

// Логический номер потока -> физический слот (шаг 9 очереди, §2.10). Поток 0 —
// геометрия — на своём месте всегда; когда включён инстансный путь, слот 1
// занят под данные экземпляра и геометрийные потоки клиента уезжают выше —
// ровно как сдвигает ненулевые потоки раскладка с атрибутом экземпляра
// (InstancedVariant). Раньше этот сдвиг делал SmartRenderer::BindVertexBuffer.
unsigned D3D9Backend::MapStream(unsigned logical) const {
  if (logical == kInstanceStream)
    return kInstanceSlot;
  if (instanceCount && logical)
    return logical + 1;
  return logical;
}

HRESULT D3D9Backend::SetStreamSlot(unsigned physical, IDirect3DVertexBuffer9* pVB,
                                   unsigned offset, unsigned stride) {
  if (physical >= kFreqStreams)
    return D3DERR_INVALIDCALL;
  StreamSlot& slot = streamSlot[physical];
  if (slot.pVB == pVB && slot.offset == offset && slot.stride == stride)
    return D3D_OK;
  slot.pVB = pVB;
  slot.offset = offset;
  slot.stride = stride;
  return pDevice->SetStreamSource(physical, pVB, offset, stride);
}

bool D3D9Backend::IsStreamBufferBound(const void* pVertexBuffer) const {
  if (!pVertexBuffer)
    return false;
  for (unsigned n = 0; n < kFreqStreams; ++n)
    if (streamSlot[n].pVB == pVertexBuffer)
      return true;
  return false;
}

HRESULT D3D9Backend::SetIndices(HBuffer indexBuffer) {
  return pDevice->SetIndices(static_cast<IDirect3DIndexBuffer9*>(indexBuffer.p));
}

HRESULT D3D9Backend::SetInputLayout(HInputLayout layout) {
  IDirect3DVertexDeclaration9* pDecl = static_cast<IDirect3DVertexDeclaration9*>(layout.p);
  // Инстансный путь со счётчиком: базовая раскладка атрибута номера экземпляра
  // не имеет, её подставляет бэкенд (кэш по указателю базовой раскладки).
  if (instanceCount && instanceCounterSource)
    pDecl = InstancedVariant(pDecl);
  return pDevice->SetVertexDeclaration(pDecl);
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

// PW_D3D_DRAWERR=1 — печатать HRESULT всех выводов, вернувших не D3D_OK.
// Нужен, чтобы отличить «D3D9-путь не рисует вывод» от «рисует, но пиксели
// другие»: по трассам (smartrenderer.cpp NoteDipTrace) клиент выпускает
// вывод-плашку в обоих путях с одинаковыми вершинами, матрицами и состоянием,
// при этом в Vulkan он красит белый прямоугольник 330x132, а в D3D9/DXVK — нет.
static unsigned NoteDrawResult(HRESULT hr, const char* what, unsigned a, unsigned b, unsigned c)
{
  static int s_on = -1;
  static unsigned s_logged = 0u;
  if (s_on < 0)
  {
    char bd[16];
    s_on = (GetEnvironmentVariableA("PW_D3D_DRAWERR", bd, sizeof(bd) - 1) > 0) ? 1 : 0;
  }
  if (hr == D3D_OK || !s_on || s_logged >= 60u)
    return hr;
  ++s_logged;
  DebugTrace("D3dDrawErr: %s hr=%08X a=%u b=%u c=%u", what, (unsigned)hr, a, b, c);
  return hr;
}

HRESULT D3D9Backend::DrawIndexed(ERenderPrimitiveType type, unsigned baseVertexIndex, unsigned minIndex,
                                 unsigned indexedVertexCount, unsigned startIndex, unsigned primitiveCount) {
  ApplyInstanceState();
  return NoteDrawResult(pDevice->DrawIndexedPrimitive(ConvertPrimitiveType(type), baseVertexIndex, minIndex,
                                                      indexedVertexCount, startIndex, primitiveCount),
                        "DrawIndexed", (unsigned)type, startIndex, primitiveCount);
}

HRESULT D3D9Backend::Draw(ERenderPrimitiveType type, unsigned startVertex, unsigned primitiveCount) {
  ApplyInstanceState();
  return NoteDrawResult(pDevice->DrawPrimitive(ConvertPrimitiveType(type), startVertex, primitiveCount),
                        "Draw", (unsigned)type, startVertex, primitiveCount);
}

HRESULT D3D9Backend::DrawIndexedUP(ERenderPrimitiveType type, unsigned minIndexValue, unsigned indexCount,
                                   unsigned vertexCount, const void* pIndexData, bool index16,
                                   const void* pVertexData, unsigned vertexStride) {
  ApplyInstanceState();
  return pDevice->DrawIndexedPrimitiveUP(ConvertPrimitiveType(type), minIndexValue, indexCount, vertexCount,
                                         pIndexData, index16 ? D3DFMT_INDEX16 : D3DFMT_INDEX32,
                                         pVertexData, vertexStride);
}

HRESULT D3D9Backend::DrawUP(ERenderPrimitiveType type, unsigned primitiveCount,
                            const void* pVertexData, unsigned vertexStride) {
  ApplyInstanceState();
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
    // kInstanceStream -> физический слот экземпляра (тот же MapStream, что и
    // для биндингов: раскладка и потоки обязаны сходиться).
    d3d_element.Stream       = static_cast<unsigned short>(MapStream(element.stream));
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

// Шаг 13.4c: GetDeclaration обратно (см. комментарий в renderbackend.h).
// GetDeclaration возвращает элементы без завершающего D3DDECL_END.
HRESULT D3D9Backend::GetInputLayoutElements(HInputLayout layout, VertexElementDescriptor* pOut,
                                            unsigned maxElements, unsigned* pCount) {
  if (pCount) *pCount = 0;
  if (!layout.p || !pOut || !pCount) return E_INVALIDARG;
  D3DVERTEXELEMENT9 decl[MAXD3DDECLLENGTH];
  UINT n = 0;
  IDirect3DVertexDeclaration9* base = static_cast<IDirect3DVertexDeclaration9*>(layout.p);
  if (FAILED(base->GetDeclaration(decl, &n))) return E_FAIL;
  if (n > maxElements) {
    DebugTrace("D3D9Backend: раскладка из %u элементов не влезает в %u", n, maxElements);
    return E_FAIL;
  }
  for (UINT i = 0; i < n; ++i)
    pOut[i] = VertexElementDescriptor(decl[i].Stream, decl[i].Offset,
                                      Convert2VertexElementType(decl[i].Type),
                                      Convert2VertexElementUsage(decl[i].Usage), decl[i].UsageIndex);
  *pCount = (unsigned)n;
  return S_OK;
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
// в логах не виден. Таблица — в renderflagsconverter.h (BackendFormatToD3D),
// здесь только обёртка: словарь форматов обязан жить в одном месте.
static D3DFORMAT D3DFormatFromBackend(EBackendFormat format) {
  D3DFORMAT fmt = D3DFMT_UNKNOWN;
  BackendFormatToD3D(format, &fmt);
  return fmt;
}

static DWORD D3DUsageFromBackend(EBackendUsage usage) {
  return D3DUsageFromBackendBits(usage);
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

// Shag 13.4c: pri perevorote D3D9-ustroistvo zhivyoT parallelno s Vulkan-bekendom,
// i ego poverkhnosti ukhodyat v SmartRenderer kak chuzhie (magiya ne SEAM). Bez
// adresa sozdaniya chuzhuyu ruchku v loge ne opoznat', pojetomu sozdanie pechataetsya.
static unsigned g_d3d9SurfLogged = 0;
static void NoteD3D9Surf(const char* what, const void* p, unsigned w, unsigned h, int kind)
{
  if (g_d3d9SurfLogged++ >= 24u) return;
  DebugTrace("D3D9Surf: %s 0x%llX %ux%u kind=%d", what, (unsigned long long)(size_t)p, w, h, kind);
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
  NoteD3D9Surf("CreateSurface", surf, width, height, (int)kind);
  *pOut = MakeBackendHandle<HSurface>(surf);
  return hr;
}

HRESULT D3D9Backend::UpdateTexture(HTexture srcTexture, HTexture dstTexture) {
  return pDevice->UpdateTexture(static_cast<IDirect3DBaseTexture9*>(srcTexture.p),
                                static_cast<IDirect3DBaseTexture9*>(dstTexture.p));
}

HRESULT D3D9Backend::LockTexture2D(HTexture texture, unsigned level, unsigned face,
                                   const BackendRect* pRect,
                                   ERenderLockType mode, void** ppData, unsigned* pRowPitch) {
  if (!texture.p || !ppData)
    return E_INVALIDARG;
  // HTexture здесь = то, что вернул CreateTexture2D (IDirect3DTexture9*).
  // Cube-текстуры через этот метод не блокируются: у seam'а нет отдельного
  // LockTextureCube, а клиент cube блокирует иными путями (замер — только
  // ReadDDS/TextureManager, они остаются D3D9-ными до шага 13.2).
  IDirect3DTexture9* tex = static_cast<IDirect3DTexture9*>(texture.p);
  D3DLOCKED_RECT lr;
  lr.pBits = 0;
  lr.Pitch = 0;
  RECT r, *pR = 0;
  if (pRect)
  {
    r.left = pRect->left; r.top = pRect->top;
    r.right = pRect->right; r.bottom = pRect->bottom;
    pR = &r;
  }
  // Cube-текстура блокируется по граням (face = D3DCUBEMAP_FACES).
  // Тип берём из самой текстуры: клиент (ReadDDS.cpp) знал его, но
  // передавал в бэкенд только void*.
  HRESULT hr;
  if (face || tex->GetType() == D3DRTYPE_CUBETEXTURE)
    hr = static_cast<IDirect3DCubeTexture9*>(texture.p)
             ->LockRect((D3DCUBEMAP_FACES)face, level, &lr, pR, ConvertRenderLockType(mode));
  else
    hr = tex->LockRect(level, &lr, pR, ConvertRenderLockType(mode));
  *ppData = lr.pBits;
  if (pRowPitch)
    *pRowPitch = (unsigned)lr.Pitch;
  return hr;
}

HRESULT D3D9Backend::UnlockTexture2D(HTexture texture, unsigned level, unsigned face) {
  if (!texture.p)
    return E_INVALIDARG;
  IDirect3DBaseTexture9* base = static_cast<IDirect3DBaseTexture9*>(texture.p);
  if (face || base->GetType() == D3DRTYPE_CUBETEXTURE)
    return static_cast<IDirect3DCubeTexture9*>(texture.p)->UnlockRect((D3DCUBEMAP_FACES)face, level);
  return static_cast<IDirect3DTexture9*>(texture.p)->UnlockRect(level);
}

// ---------------------------------------------------------------------------
// Шаг 13.2: описание текстуры, mip-цепочка и блокировка поверхности.
//
// До этой правки клиент читал D3DSURFACE_DESC и счётчик уровней напрямую из COM
// (texture.cpp) и сам строил mip'ы по COM-поверхностям. Здесь те же вызовы, но
// через seam: D3D9-путь обязан остаться побитово прежним, а нейтральный словарь
// не должен терять биты — поэтому round-trip проверяется здесь, где настоящие
// D3D9-значения ещё видны.

// Проверка round-trip: нейтральный словарь -> D3D9 должно давать ровно то, что
// сообщил COM. Расхождение = словарь потерял бит (например пул или DYNAMIC), и
// молча восстановленный desc уронит текстуру при потере устройства.
static void CheckTextureInfoRoundTrip(const D3DSURFACE_DESC& d, unsigned levels,
                                      const IRenderBackend::BackendTextureInfo& info) {
  static unsigned s_checked = 0, s_bad = 0;
  ++s_checked;
  D3DFORMAT fmt = D3DFMT_UNKNOWN;
  D3DPOOL   pool = D3DPOOL_DEFAULT;
  DWORD     usage = 0;
  bool ok = BackendFormatToD3D(info.format, &fmt) &&
            BackendPoolUsageToD3D(info.pool, info.usage, &pool, &usage);
  if (ok && (fmt != d.Format || pool != d.Pool || usage != d.Usage ||
             info.width != d.Width || info.height != d.Height || info.levels != levels))
    ok = false;
  if (ok)
    return;
  ++s_bad;
  if (s_bad <= 20)
  {
    DebugTrace("D3D9Backend: round-trip описания текстуры разошёлся (проверено %u): "
               "COM %ux%u fmt %s pool %d usage 0x%X levels %u",
               s_checked, d.Width, d.Height, (unsigned)d.Format, (int)d.Pool,
               (unsigned)d.Usage, levels);
    DebugTrace("D3D9Backend:   через seam %ux%u fmt %d pool %d usage 0x%X levels %u",
               info.width, info.height, (int)info.format, (int)info.pool,
               (unsigned)usage, info.levels);
  }
}

HRESULT D3D9Backend::GetTextureInfo(HTexture texture, BackendTextureInfo* pInfo) {
  if (!texture.p || !pInfo)
    return E_INVALIDARG;
  IDirect3DBaseTexture9* base = static_cast<IDirect3DBaseTexture9*>(texture.p);
  D3DSURFACE_DESC d;
  ZeroMemory(&d, sizeof(d));
  // GetLevelDesc(D3DSURFACE_DESC) есть только у 2D- и cube-текстур, но не у
  // базового IDirect3DBaseTexture9 — тип берём из GetType().
  HRESULT hr = E_INVALIDARG;
  if (base->GetType() == D3DRTYPE_TEXTURE)
    hr = static_cast<IDirect3DTexture9*>(texture.p)->GetLevelDesc(0, &d);
  else if (base->GetType() == D3DRTYPE_CUBETEXTURE)
    hr = static_cast<IDirect3DCubeTexture9*>(texture.p)->GetLevelDesc(0, &d);
  if (FAILED(hr))
    return hr;
  const unsigned levels = base->GetLevelCount();
  pInfo->width  = d.Width;
  pInfo->height = d.Height;
  pInfo->levels = levels;
  pInfo->format = D3DFormatToBackend(d.Format);
  pInfo->usage  = D3DUsageToBackend(d.Usage);
  pInfo->pool   = D3DPoolToBackend(d.Pool, d.Usage);
  pInfo->cube   = (base->GetType() == D3DRTYPE_CUBETEXTURE);
  CheckTextureInfoRoundTrip(d, levels, *pInfo);
  return S_OK;
}

// Описание поверхности (шаг 13.2): прежний путь — IDirect3DSurface9::GetDesc в
// клиенте (rendersurface.cpp, renderer.cpp). levels у поверхности — 1.
HRESULT D3D9Backend::GetSurfaceInfo(HSurface surface, BackendTextureInfo* pInfo) {
  if (!surface.p || !pInfo)
    return E_INVALIDARG;
  IDirect3DSurface9* pSurf = static_cast<IDirect3DSurface9*>(surface.p);
  D3DSURFACE_DESC d;
  ZeroMemory(&d, sizeof(d));
  const HRESULT hr = pSurf->GetDesc(&d);
  if (FAILED(hr))
    return hr;
  pInfo->width  = d.Width;
  pInfo->height = d.Height;
  pInfo->levels = 1u;
  pInfo->format = D3DFormatToBackend(d.Format);
  pInfo->usage  = D3DUsageToBackend(d.Usage);
  pInfo->pool   = D3DPoolToBackend(d.Pool, d.Usage);
  pInfo->cube   = false;   // поверхность уровня cube-текстуры — не cube
  CheckTextureInfoRoundTrip(d, 1u, *pInfo);
  return S_OK;
}

// Прежний путь — GetSurfaceLevel в клиенте (Texture2D::GetSurface); владение
// переходит к вызывающему, как у COM-вызова (умный указатель не AddRef'ит).
HRESULT D3D9Backend::GetTextureSurface(HTexture texture, unsigned level, HSurface* pOut) {
  if (!texture.p || !pOut)
    return E_INVALIDARG;
  IDirect3DSurface9* pSurf = 0;
  const HRESULT hr = static_cast<IDirect3DTexture9*>(texture.p)->GetSurfaceLevel(level, &pSurf);
  if (FAILED(hr) || !pSurf)
    return hr;
  *pOut = MakeBackendHandle<HSurface>(pSurf);
  return S_OK;
}

// Перенос mip-цепочки из texture.cpp (шаг 13.2) — без изменений: те же
// GetSurfaceLevel + StretchRect через seam.
static void GenerateSubLevels_1(uint mipLevels, IDirect3DTexture9* pTex) {
  IDirect3DSurface9 *pSurf = 0;
  HRESULT hr = pTex->GetSurfaceLevel(0, &pSurf);
  DXSurfaceRef pSrc(pSurf, false);
  NI_DX_THROW(hr, "GenerateSubLevels_1: failed to lock");

  for(uint n = 1; n < mipLevels; ++n) {
    hr = pTex->GetSurfaceLevel(n, &pSurf);
    NI_DX_THROW(hr, "GenerateSubLevels_1: failed to lock");

    DXSurfaceRef pDest(pSurf, false);
    hr = GetBackend()->StretchRect(MakeBackendHandle<HSurface>(Get(pSrc)), 0,
                                   MakeBackendHandle<HSurface>(Get(pDest)), 0,
                                   IRenderBackend::bfLinear);
    NI_DX_THROW(hr, "GenerateSubLevels_1: failed to stretch");

    Swap(pSrc, pDest);
  }
}

static void GenerateSubLevels_2(uint mipLevels, IDirect3DTexture9* pTex) {
  if(mipLevels <= 1)
    return;

  IDirect3DSurface9 *pSurf = 0;
  HRESULT hr = pTex->GetSurfaceLevel(0, &pSurf);
  DXSurfaceRef pSrc(pSurf, false);
  NI_DX_THROW(hr, "GenerateSubLevels_2: failed to lock");

  D3DSURFACE_DESC desc;
  hr = pTex->GetLevelDesc(1, &desc);
  HSurface hSurf;
  hr = GetBackend()->CreateSurface(ssRenderTarget, desc.Width, desc.Height,
                                   D3DFormatToBackend(desc.Format), BACKEND_POOL_DEFAULT, &hSurf);
  pSurf = static_cast<IDirect3DSurface9*>(hSurf.p);
  DXSurfaceRef tmp(pSurf, false);
  NI_DX_THROW(hr, "GenerateSubLevels_2: failed to create temporary surface");

  BackendRect rect = MakeBackendRect(0, 0, 0, 0);
  for(uint n = 1; n < mipLevels; ++n) {
    hr = pTex->GetLevelDesc(n, &desc);
    rect.right = desc.Width;
    rect.bottom = desc.Height;
    hr = GetBackend()->StretchRect(MakeBackendHandle<HSurface>(Get(pSrc)), 0,
                                   MakeBackendHandle<HSurface>(Get(tmp)), &rect,
                                   IRenderBackend::bfLinear);
    NI_DX_THROW(hr, "GenerateSubLevels_2: failed to stretch");

    hr = pTex->GetSurfaceLevel(n, &pSurf);
    NI_DX_THROW(hr, "GenerateSubLevels_2: failed to lock");

    pSrc.Attach(pSurf);
    hr = GetBackend()->StretchRect(MakeBackendHandle<HSurface>(Get(tmp)), &rect,
                                   MakeBackendHandle<HSurface>(Get(pSrc)), 0,
                                   IRenderBackend::bfLinear);
    NI_DX_THROW(hr, "GenerateSubLevels_2: failed to stretch");
  }
}

HRESULT D3D9Backend::GenerateMipmaps(HTexture texture) {
  if (!texture.p)
    return E_INVALIDARG;
  IDirect3DBaseTexture9* base = static_cast<IDirect3DBaseTexture9*>(texture.p);
  // Выбор способа — за бэкендом (ветки те же, что были в texture.h/texture.cpp:
  // на настоящем драйвере autogen есть, под Wine его нет, под CrossOver —
  // другая последовательность StretchRect).
  if(!Compatibility::IsRunnedUnderWine())
  {
    // GenerateMipSubLevels возвращает void (d3d9.h:1122) — статуса у него нет
    base->GenerateMipSubLevels();
    return S_OK;
  }
  if(D3DRTYPE_TEXTURE != base->GetType())
    // Под Wine цепочку для cube клиент не строил и раньше (TextureCube не
    // переопределял GenerateMipSubLevels_), так что отказа это не меняет.
    return E_FAIL;
  IDirect3DTexture9* tex = static_cast<IDirect3DTexture9*>(base);
  const uint mipLevels = tex->GetLevelCount();
  if(Compatibility::IsRunnedUnderCrossOverWine())
    GenerateSubLevels_1(mipLevels, tex);
  else
    GenerateSubLevels_2(mipLevels, tex);
  return S_OK;
}

HRESULT D3D9Backend::LockSurface(HSurface surface, const BackendRect* pRect,
                                 ERenderLockType mode, void** ppData, unsigned* pRowPitch) {
  if (!surface.p || !ppData)
    return E_INVALIDARG;
  IDirect3DSurface9* surf = static_cast<IDirect3DSurface9*>(surface.p);
  D3DLOCKED_RECT lr;
  lr.pBits = 0;
  lr.Pitch = 0;
  RECT r, *pR = 0;
  if (pRect)
  {
    r.left = pRect->left; r.top = pRect->top;
    r.right = pRect->right; r.bottom = pRect->bottom;
    pR = &r;
  }
  const HRESULT hr = surf->LockRect(&lr, pR, ConvertRenderLockType(mode));
  *ppData = lr.pBits;
  if (pRowPitch)
    *pRowPitch = (unsigned)lr.Pitch;
  return hr;
}

HRESULT D3D9Backend::UnlockSurface(HSurface surface) {
  if (!surface.p)
    return E_INVALIDARG;
  return static_cast<IDirect3DSurface9*>(surface.p)->UnlockRect();
}

// Модель состояний приходит от менеджера перед каждым выводом. D3D9-бэкенду
// она не нужна (состояния уже записаны в устройство), но вызов оставлен как
// контроль полноты модели: если тень менеджера отстаёт от устройства — значит
// кто-то пишет прямо в D3D9, и на Vulkan/DX12 это потерянное состояние.
// Заметить это можно только здесь, где есть с чем сравнить. Проверка дорогая
// (GetRenderState на каждое состояние), поэтому под dev-var и не каждый кадр:
//   setvar verify_state_model 1
// Регистрация — REGISTER_VAR, а не REGISTER_DEV_VAR: во shipping-конфигурации
// (Commands.h:457-469: _SHIPPING без _RELEASE) REGISTER_DEV_VAR сворачивается в
// пустоту, и переключатель молча исчезает вместе с проверкой. Так же сделано в
// multishader.cpp:31,51 (dump_shader_index, shader_target).
static bool s_verifyStateModel = false;
REGISTER_VAR("verify_state_model", s_verifyStateModel, STORAGE_NONE)

void D3D9Backend::SetPipelineState(const BackendPipelineState& /*state*/, unsigned long long /*stateKey*/) {
  if (!s_verifyStateModel)
    return;
  static unsigned s_verifyTick = 0;
  if ((++s_verifyTick % 120u) != 0u)
    return;
  RenderStatesManager* const sm = GetStatesManager();
  if (!sm)
    return;
  const unsigned bad = sm->VerifyShadowAgainstDevice(6u);
  // Сводка печатается и при нуле: молчание в гейте неотличимо от «проверка не
  // запускалась» (переключатель не прочитан, SetPipelineState не вызывается).
  // Периодичность — не каждый кадр, но достаточно часто, чтобы покрыть бой
  // (проверка только на загрузке экрана ничего не доказывает).
  static unsigned s_checks = 0, s_badTotal = 0, s_reported = 0;
  ++s_checks;
  s_badTotal += bad;
  if (s_checks == 1u || (s_checks % 200u) == 0u || (bad && s_badTotal != s_reported))
  {
    s_reported = s_badTotal;
    DebugTrace("D3D9Backend: сверка модели состояний с устройством — проверок: %u, расхождений: %u",
               s_checks, s_badTotal);
  }
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

// Шаг 13.4c: размер буфера. Клиент (учёт геометрий, CRC)
// читал D3DVERTEXBUFFER_DESC/D3DINDEXBUFFER_DESC напрямую по ручке;
// тип (VB или IB) берётся из самого ресурса — вызывающий передаёт void*.
HRESULT D3D9Backend::GetBufferInfo(HBuffer buffer, unsigned* pByteSize) {
  if (!buffer.p) return D3DERR_INVALIDCALL;
  IDirect3DResource9* res = static_cast<IDirect3DResource9*>(buffer.p);
  const D3DRESOURCETYPE type = res->GetType();
  if (type == D3DRTYPE_VERTEXBUFFER) {
    D3DVERTEXBUFFER_DESC d;
    const HRESULT hr = static_cast<IDirect3DVertexBuffer9*>(buffer.p)->GetDesc(&d);
    if (SUCCEEDED(hr) && pByteSize) *pByteSize = (unsigned)d.Size;
    return hr;
  }
  if (type == D3DRTYPE_INDEXBUFFER) {
    D3DINDEXBUFFER_DESC d;
    const HRESULT hr = static_cast<IDirect3DIndexBuffer9*>(buffer.p)->GetDesc(&d);
    if (SUCCEEDED(hr) && pByteSize) *pByteSize = (unsigned)d.Size;
    return hr;
  }
  return D3DERR_INVALIDCALL;
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
  {
    pOut->p = pSurf;   // владеющая
    NoteD3D9Surf("GetDepthStencil", pSurf, 0, 0, -1);
  }
  return hr;
}

HRESULT D3D9Backend::GetBackBuffer(HSurface* pOut) {
  if (!pOut)
    return E_INVALIDARG;
  IDirect3DSurface9* pBB = 0;
  const HRESULT hr = pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBB);
  if (SUCCEEDED(hr)) {
    pOut->p = pBB;
    NoteD3D9Surf("GetBackBuffer", pBB, 0, 0, -2);
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

// Состояние инстансинга применяется дельтой и только перед рисованием:
// биндинги потоков к этому моменту уже выполнены (исторический порядок
// ApplyStates — внутри Draw*Primitive после биндингов), и переставлять его
// нечем оправдывать. Сюда же входит привязка слота данных экземпляра — она
// зависит от instanceCount (размер счётчика), а вызывающий вправе задать
// источник и до, и после SetInstanceCount.
void D3D9Backend::ApplyInstanceState() {
  unsigned requested[kFreqStreams];
  for (unsigned n = 0; n < kFreqStreams; ++n)
    requested[n] = 1;

  if (instanceCount) {
    // Поток 0 — геометрия: проходится заново на каждый экземпляр.
    requested[0] = D3DSTREAMSOURCE_INDEXEDDATA | instanceCount;
    // Слот экземпляра: одно значение на экземпляр.
    requested[kInstanceSlot] = D3DSTREAMSOURCE_INSTANCEDATA | 1;

    if (instanceCounterSource) {
      if (SUCCEEDED(EnsureInstanceCounter(instanceCount)))
        SetStreamSlot(kInstanceSlot, pInstanceCounterVB, 0, sizeof(int));
    } else if (instanceSourceVB) {
      SetStreamSlot(kInstanceSlot, instanceSourceVB, instanceSourceOffset, instanceSourceStride);
    }
  }

  for (unsigned n = 0; n < kFreqStreams; ++n)
    if (streamFreq[n] != requested[n])
      pDevice->SetStreamSourceFreq(n, streamFreq[n] = requested[n]);
}

// Источник данных экземпляра (шаг 9 очереди, §2.10). Раньше счётчик 0..N-1
// жил в клиенте (SmartRenderer::InitIdxVB / s_pInstanceIndexVB) и туда же
// уходело решение «какой слот под ним занят».
// Здесь только запись намерения: привязка к слоту — ленивая, в
// ApplyInstanceState (перед рисованием), поэтому порядок SetInstanceCount /
// SetInstanceSource / биндингов геометрии не важен.
HRESULT D3D9Backend::SetInstanceSource(HBuffer perInstanceData, unsigned stride, unsigned offset) {
  instanceCounterSource = (perInstanceData.p == 0);
  instanceSourceVB = static_cast<IDirect3DVertexBuffer9*>(perInstanceData.p);
  instanceSourceStride = stride;
  instanceSourceOffset = offset;
  return D3D_OK;
}

HRESULT D3D9Backend::EnsureInstanceCounter(unsigned count) {
  if (pInstanceCounterVB && instanceCounterEntries >= count)
    return D3D_OK;

  const unsigned entries = count < 64 ? 64 : count;
  if (pInstanceCounterVB) {
    pInstanceCounterVB->Release();
    pInstanceCounterVB = 0;
    instanceCounterEntries = 0;
  }

  IDirect3DVertexBuffer9* pVB = 0;
  HRESULT hr = pDevice->CreateVertexBuffer(entries * sizeof(int), 0, 0 /* FVF: без FVF, раскладка у декларации */, D3DPOOL_MANAGED, &pVB, NULL);
  if (FAILED(hr) || !pVB)
    return hr;

  // Значение — номер экземпляра; формат int (не float): раскладка читает
  // младшие 16 бит, float-счётчик дал бы мусор в вершинах (грабля
  // InstancedMeshResource::Init).
  int* pValues = 0;
  hr = pVB->Lock(0, entries * sizeof(int), reinterpret_cast<void**>(&pValues), 0);
  if (SUCCEEDED(hr)) {
    for (unsigned n = 0; n < entries; ++n)
      pValues[n] = static_cast<int>(n);
    pVB->Unlock();
  }

  pInstanceCounterVB = pVB;
  instanceCounterEntries = entries;
  return hr;
}

// Раскладка с атрибутом номера экземпляра: сдвиг ненулевых потоков на 1 вверх
// (их забирает слот экземпляра — тот же сдвиг, что делает MapStream для
// биндингов) и SHORT2/BLENDINDICES0 в слот экземпляра (шиммер HLSL читает его
// как InstanceId). Кэшируется по базовой раскладке — раньше этим кэшем владел
// SmartRenderer (DeclMap/s_instancedVDecl).
IDirect3DVertexDeclaration9* D3D9Backend::InstancedVariant(IDirect3DVertexDeclaration9* base) {
  if (!base)
    return 0;

  void* const key = static_cast<void*>(base);
  InstancedDeclMap::const_iterator found = instancedDecls.find(key);
  if (found != instancedDecls.end())
    return found->second;

  D3DVERTEXELEMENT9 decl[MAXD3DDECLLENGTH];
  UINT numElements = 0;
  if (FAILED(base->GetDeclaration(decl, &numElements)))
    return base;
  // Последний элемент — D3DDECL_END(); плюс новый элемент перед ним.
  if (numElements < 2 || numElements >= MAXD3DDECLLENGTH)
    return base;

  for (UINT n = 0; n + 1 < numElements; ++n)
    if (decl[n].Usage == D3DDECLUSAGE_BLENDINDICES && decl[n].UsageIndex == 0)
      return base;   // атрибут номера экземпляра уже описан (InstancedMeshResource)

  for (UINT n = 0; n + 1 < numElements; ++n)
    if (decl[n].Stream)
      ++decl[n].Stream;

  static const D3DVERTEXELEMENT9 instanceElement = { kInstanceSlot, 0, D3DDECLTYPE_SHORT2, 0,
                                                     D3DDECLUSAGE_BLENDINDICES, 0 };
  static const D3DVERTEXELEMENT9 declEnd = D3DDECL_END();

  decl[numElements - 1] = instanceElement;
  decl[numElements] = declEnd;

  IDirect3DVertexDeclaration9* pNew = 0;
  if (FAILED(pDevice->CreateVertexDeclaration(decl, &pNew)) || !pNew)
    return base;

  instancedDecls[key] = pNew;
  return pNew;
}

void D3D9Backend::ReleaseInstancedDecls() {
  for (InstancedDeclMap::iterator i = instancedDecls.begin(); i != instancedDecls.end(); ++i)
    if (i->second && static_cast<void*>(i->second) != i->first)
      i->second->Release();
  instancedDecls.clear();
}

void D3D9Backend::InvalidateBoundState() {
  instanceCount = 0;
  instanceCounterSource = false;
  instanceSourceVB = 0;
  instanceSourceStride = 0;
  instanceSourceOffset = 0;
  for (unsigned n = 0; n < kFreqStreams; ++n) {
    streamFreq[n] = 1;
    // Устройство после Reset() (или нового CreateDevice) без биндингов — и
    // кэш дедупа обязан об этом знать, иначе первый же биндинг того же буфера
    // будет пропущен.
    streamSlot[n].pVB = 0;
    streamSlot[n].offset = 0;
    streamSlot[n].stride = 0;
  }
  // Декларации живут на устройстве — после сброса/пересоздания они мертвы.
  ReleaseInstancedDecls();
}
} // namespace Render
