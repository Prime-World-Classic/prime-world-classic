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

  void ApplyInstanceFreqs();

public:
  explicit D3D9Backend(IDirect3DDevice9* _pDevice) : pDevice(_pDevice), instanceCount(0) {
    // Дефолт D3D9 — частота 1 у всех потоков; после Reset() устройство
    // возвращается к нему же (см. InvalidateBoundState).
    for (unsigned n = 0; n < kFreqStreams; ++n)
      streamFreq[n] = 1;
  }

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
  virtual HRESULT CreateInstancedInputLayout(HInputLayout base, HInputLayout* pOut);

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

  virtual HRESULT CreateVertexBuffer(unsigned size, EBackendPool pool, HBuffer* pOut);
  virtual HRESULT CreateIndexBuffer(unsigned size, bool index32, EBackendPool pool, HBuffer* pOut);

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
