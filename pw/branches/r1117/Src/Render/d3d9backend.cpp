#include "stdafx.h"
#include "d3d9backend.h"

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

HRESULT D3D9Backend::Present(HWND window, const RECT* pSrcRect, const RECT* pDstRect) {
  return pDevice->Present(pSrcRect, pDstRect, window, NULL);
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

void D3D9Backend::SetScissorRect(const RECT* pRect) {
  pDevice->SetScissorRect(pRect);
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
