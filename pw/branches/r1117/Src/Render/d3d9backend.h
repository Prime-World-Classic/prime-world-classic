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

public:
  explicit D3D9Backend(IDirect3DDevice9* _pDevice) : pDevice(_pDevice) {}

  // --- кадр ---
  virtual HRESULT BeginScene();
  virtual HRESULT EndScene();
  virtual HRESULT Present(NativeWindow window, const BackendRect* pSrcRect, const BackendRect* pDstRect);

  // --- область вывода и маска отсечения ---
  virtual void GetViewport(BackendViewport& vp);
  virtual void SetScissorRect(const BackendRect* pRect);

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

  // --- создание ресурсов ---
  virtual HRESULT CreateVertexShader(const void* pShaderCode, unsigned codeSize, HShader* pOut);
  virtual HRESULT CreatePixelShader(const void* pShaderCode, unsigned codeSize, HShader* pOut);
  virtual HRESULT CreateInputLayout(const VertexFormatDescriptor& descr, HInputLayout* pOut);

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
