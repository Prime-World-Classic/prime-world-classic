#pragma once
//////////////////////////////////////////////////////////////////////////
// IRenderBackend поверх IDirect3DDevice9 (этап 2 плана NVRHI).
//
// Временная реализация: удаляется вместе с D3D9 на этапе 4. Устройство ей не
// владеет — владельцем остаётся Renderer. Reset() в D3D9 не меняет объект
// устройства, поэтому бэкенд переживает сброс без пересоздания; при повторном
// CreateDevice() Renderer пересоздаёт и бэкенд.

#include "renderbackend.h"

// Прямое объявление, а не #include <d3d9.h>: заголовок подключается из
// renderer.h, а устройство в этом билде и так видно через stdafx.h.
// Объявление — на глобальном уровне: IDirect3DDevice9 живёт вне namespace Render.
struct IDirect3DDevice9;

namespace Render
{

// D3DVIEWPORT9 -> BackendViewport: кэш вьюпорта в SmartRenderer пока хранит
// D3D9-структуру, конвертация на месте вызова (этап 3 уберёт и кэш).
inline BackendViewport MakeBackendViewport(const D3DVIEWPORT9& vp) {
  BackendViewport bvp;
  bvp.x = vp.X;
  bvp.y = vp.Y;
  bvp.width = vp.Width;
  bvp.height = vp.Height;
  bvp.minZ = vp.MinZ;
  bvp.maxZ = vp.MaxZ;
  return bvp;
}

class D3D9Backend : public IRenderBackend {
  IDirect3DDevice9* pDevice;

  // Буфер под BackendSystemInfo::description: D3DADAPTER_IDENTIFIER9 живёт на
  // стеке вызова, а ссылку на него наружу возвращать нельзя.
  char adapterDescription[128];

  // Инстансинг D3D9 устроен на частотах потоков: поток геометрии повторяется
  // (INDEXEDDATA|count), поток экземпляра читается один раз на экземпляр
  // (INSTANCEDATA|1). Частоты — состояние устройства, поэтому они кэшируются
  // здесь и применяются дельтой перед каждым рисованием (исторически этим
  // занимался SmartRenderer::ApplyStates — ушёл сюда целиком).
  // Граница 16 — прежний SmartRenderer::MAX_STREAMS_COUNT; в клиенте инстансные
  // пути используют потоки 0 и 1.
  static const unsigned kFreqStreams = 16;
  unsigned streamFreq[kFreqStreams];   // что реально выставлено на устройстве
  unsigned instanceCount;              // 0 — инстансный путь выключен

  // Физический слот, который D3D9 читает с частотой INSTANCEDATA.
  static const unsigned kInstanceSlot = 1;

  // Таблица «что стоит в вершинном слоте» — по ней дедупятся вызовы устройства.
  // Живёт здесь, а не в SmartRenderer: слоты выбирает бэкенд (MapStream), и
  // клиентская таблица по логическим номерам разъехалась бы с устройством,
  // когда геометрийный поток уезжает на слот выше (или наоборот).
  struct StreamSlot {
    IDirect3DVertexBuffer9* pVB;
    unsigned offset;
    unsigned stride;
  };
  StreamSlot streamSlot[kFreqStreams];

  // Счётчик экземпляров 0..N-1 для SetInstanceSource без буфера. D3D9 читает
  // его атрибутом SHORT2 (младшие 16 бит int'а; раскладка обязана быть
  // int'овой, float-счётчик ломает вершины — грабля InstancedMeshResource).
  IDirect3DVertexBuffer9* pInstanceCounterVB;
  unsigned instanceCounterEntries;
  // Источник данных экземпляра: свой счётчик или per-instance буфер клиента
  // (партиклы). Записывается в слот экземпляра лениво, перед рисованием —
  // порядок SetInstanceCount / SetInstanceSource / биндингов геометрии при этом
  // не важен (на Vulkan биндинг буфера и count в draw-вызове тоже независимы).
  bool instanceCounterSource;
  IDirect3DVertexBuffer9* instanceSourceVB;
  unsigned instanceSourceStride;
  unsigned instanceSourceOffset;

  // Базовая раскладка -> её вариант с атрибутом номера экземпляра
  // (InstancedVariant). Кэш действителен, пока живо устройство:
  // InvalidateBoundState его чистит.
  // Ключ — void*: для указателей в nstl::hash_map нужна специализация
  // nstl::hash, она есть только для void* (так же, как в прежнем DeclMap).
  typedef nstl::hash_map<void*, IDirect3DVertexDeclaration9*> InstancedDeclMap;
  InstancedDeclMap instancedDecls;

  void ApplyInstanceState();  // частоты потоков + слот данных экземпляра (дельтой, перед рисованием)
  // Логический номер потока -> физический слот устройства.
  unsigned MapStream(unsigned logical) const;
  HRESULT SetStreamSlot(unsigned physical, IDirect3DVertexBuffer9* pVB, unsigned offset, unsigned stride);
  HRESULT EnsureInstanceCounter(unsigned count);
  // Раскладка с атрибутом номера экземпляра (или base, если он там уже есть).
  IDirect3DVertexDeclaration9* InstancedVariant(IDirect3DVertexDeclaration9* base);
  void ReleaseInstancedDecls();

public:
  explicit D3D9Backend(IDirect3DDevice9* _pDevice) : pDevice(_pDevice), instanceCount(0),
      pInstanceCounterVB(0), instanceCounterEntries(0), instanceCounterSource(false),
      instanceSourceVB(0), instanceSourceStride(0), instanceSourceOffset(0) {
    adapterDescription[0] = 0;
    // Дефолт D3D9 — частота 1 у всех потоков; после Reset() устройство
    // возвращается к нему же (см. InvalidateBoundState).
    for (unsigned n = 0; n < kFreqStreams; ++n) {
      streamFreq[n] = 1;
      streamSlot[n].pVB = 0;
      streamSlot[n].offset = 0;
      streamSlot[n].stride = 0;
    }
  }
  ~D3D9Backend() {
    ReleaseInstancedDecls();
    if (pInstanceCounterVB) {
      pInstanceCounterVB->Release();
      pInstanceCounterVB = 0;
    }
  }

  // --- сведения об адаптере ---
  virtual HRESULT GetSystemInfo(BackendSystemInfo& out);

  // --- кадр ---
  virtual HRESULT BeginScene();
  virtual HRESULT EndScene();
  virtual HRESULT Present(NativeWindow window, const BackendRect* pSrcRect, const BackendRect* pDstRect);

  // --- область вывода и маска отсечения ---
  virtual void GetViewport(BackendViewport& vp);
  virtual void SetScissorRect(const BackendRect* pRect);
  virtual void GetScissorRect(BackendRect* pOut);

  // --- очистка ---
  virtual HRESULT Clear(unsigned clearFlags, unsigned colorRGBA, float z, unsigned stencil);

  // --- константы шейдеров ---
  virtual HRESULT SetVertexShaderConstants(unsigned startRegister, unsigned registerCount, const void* pData);
  virtual HRESULT SetPixelShaderConstants(unsigned startRegister, unsigned registerCount, const void* pData);
  virtual HRESULT GetShaderConstants(EShaderConstStage stage, unsigned startRegister,
                                     unsigned registerCount, void* pData);

  // --- биндинги ---
  virtual HRESULT SetStreamSource(unsigned slot, HBuffer vertexBuffer, unsigned byteOffset, unsigned stride);
  virtual HRESULT SetIndices(HBuffer indexBuffer);
  virtual HRESULT SetInputLayout(HInputLayout layout);
  virtual HRESULT SetVertexShader(HShader shader);
  virtual HRESULT SetPixelShader(HShader shader);
  virtual HRESULT SetTexture(unsigned samplerIndex, HTexture texture);
  virtual HRESULT SetRenderTarget(unsigned index, HSurface surface);
  virtual HRESULT SetDepthStencil(HSurface surface);
  virtual HRESULT GetDepthStencil(HSurface* pOut);
  virtual HRESULT SetViewport(const BackendViewport& vp);

  // --- вывод ---
  virtual HRESULT DrawIndexed(ERenderPrimitiveType type, unsigned baseVertexIndex, unsigned minIndex,
                              unsigned indexedVertexCount, unsigned startIndex, unsigned primitiveCount);
  virtual HRESULT Draw(ERenderPrimitiveType type, unsigned startVertex, unsigned primitiveCount);
  virtual HRESULT DrawIndexedUP(ERenderPrimitiveType type, unsigned minIndexValue, unsigned indexCount,
                                unsigned vertexCount, const void* pIndexData, bool index16,
                                const void* pVertexData, unsigned vertexStride);
  virtual HRESULT DrawUP(ERenderPrimitiveType type, unsigned primitiveCount,
                         const void* pVertexData, unsigned vertexStride);

  // --- инстансинг ---
  virtual HRESULT SetInstanceCount(unsigned count);
  virtual HRESULT SetInstanceSource(HBuffer perInstanceData, unsigned stride, unsigned offset);
  virtual bool IsStreamBufferBound(const void* pVertexBuffer) const;

  virtual void InvalidateBoundState();

  // --- создание ресурсов ---
  virtual HRESULT CreateVertexShader(const void* pShaderCode, unsigned codeSize, HShader* pOut);
  virtual HRESULT CreatePixelShader(const void* pShaderCode, unsigned codeSize, HShader* pOut);
  virtual HRESULT CreateInputLayout(const VertexFormatDescriptor& descr, HInputLayout* pOut);

  virtual HRESULT CreateTexture2D(unsigned width, unsigned height, unsigned mipLevels,
                                  EBackendUsage usage, EBackendFormat format, EBackendPool pool,
                                  HTexture* pOut);
  virtual HRESULT CreateTextureCube(unsigned edgeLen, unsigned mipLevels,
                                    EBackendUsage usage, EBackendFormat format, EBackendPool pool,
                                    HTexture* pOut);
  virtual HRESULT CreateSurface(EBackendSurfaceKind kind, unsigned width, unsigned height,
                                EBackendFormat format, EBackendPool pool, HSurface* pOut);
  virtual HRESULT UpdateTexture(HTexture srcTexture, HTexture dstTexture);
  virtual HRESULT LockTexture2D(HTexture texture, unsigned level, const BackendRect* pRect,
                                ERenderLockType mode, void** ppData, unsigned* pRowPitch);
  virtual HRESULT UnlockTexture2D(HTexture texture, unsigned level);

  virtual HRESULT CreateVertexBuffer(unsigned size, EBackendPool pool, HBuffer* pOut);
  virtual HRESULT CreateIndexBuffer(unsigned size, bool index32, EBackendPool pool, HBuffer* pOut);

  // --- запись в буферы ---
  virtual HRESULT LockVertexBuffer(HBuffer vb, unsigned offset, unsigned size,
                                   ERenderLockType mode, void** ppData);
  virtual HRESULT UnlockVertexBuffer(HBuffer vb);
  virtual HRESULT LockIndexBuffer(HBuffer ib, unsigned offset, unsigned size,
                                  ERenderLockType mode, void** ppData);
  virtual HRESULT UnlockIndexBuffer(HBuffer ib);

  // --- поверхности ---
  virtual HRESULT GetRenderTargetData(HSurface srcSurface, HSurface dstSurface);
  virtual HRESULT ColorFill(HSurface surface, unsigned colorRGBA);
  virtual HRESULT StretchRect(HSurface src, const BackendRect* pSrcRect, HSurface dst,
                              const BackendRect* pDstRect, EBackendFilter filter);
  virtual HRESULT ReadSurface(HSurface srcSurface, const BackendRect* pCropRect, void* pDstBuffer,
                              unsigned dstRowPitch, EBackendPixelLayout* pOutLayout);
  virtual HRESULT GetBackBuffer(HSurface* pOut);
  virtual HRESULT GetRenderTarget(unsigned index, HSurface* pOut);

  virtual void SetClipPlane(bool enable, const float* pPlaneEquation);

  virtual void EvictManagedResources();
};

} // namespace Render
