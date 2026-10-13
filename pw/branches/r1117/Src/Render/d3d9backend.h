#pragma once
//////////////////////////////////////////////////////////////////////////
// IRenderBackend поверх IDirect3DDevice9 (этап 2 плана NVRHI).
//
// Временная реализация: удаляется вместе с D3D9 на этапе 4. Устройство ей не
// владеет — владельцем остаётся Renderer. Reset() в D3D9 не меняет объект
// устройства, поэтому бэкенд переживает сброс без пересоздания; при повторном
// CreateDevice() Renderer пересоздаёт и бэкенд.

#include "renderbackend.h"

struct IDirect3DDevice9;

class D3D9Backend : public IRenderBackend {
  IDirect3DDevice9* pDevice;

public:
  explicit D3D9Backend(IDirect3DDevice9* _pDevice) : pDevice(_pDevice) {}

  // --- кадр ---
  virtual HRESULT BeginScene();
  virtual HRESULT EndScene();
  virtual HRESULT Present(HWND window, const RECT* pSrcRect, const RECT* pDstRect);

  // --- область вывода и маска отсечения ---
  virtual void GetViewport(BackendViewport& vp);
  virtual void SetScissorRect(const RECT* pRect);

  // --- очистка ---
  virtual HRESULT Clear(unsigned clearFlags, unsigned colorRGBA, float z, unsigned stencil);

  // --- константы шейдеров ---
  virtual HRESULT SetVertexShaderConstants(unsigned startRegister, unsigned registerCount, const void* pData);
  virtual HRESULT SetPixelShaderConstants(unsigned startRegister, unsigned registerCount, const void* pData);

  virtual void SetClipPlane(bool enable, const float* pPlaneEquation);

  virtual void EvictManagedResources();
};
