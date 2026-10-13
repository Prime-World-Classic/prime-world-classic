#include "stdafx.h"
#include "d3d9backend.h"
#include "renderflagsconverter.h"

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
  return pDevice->DrawIndexedPrimitive(ConvertPrimitiveType(type), baseVertexIndex, minIndex,
                                       indexedVertexCount, startIndex, primitiveCount);
}

HRESULT D3D9Backend::Draw(ERenderPrimitiveType type, unsigned startVertex, unsigned primitiveCount) {
  return pDevice->DrawPrimitive(ConvertPrimitiveType(type), startVertex, primitiveCount);
}

HRESULT D3D9Backend::DrawIndexedUP(ERenderPrimitiveType type, unsigned minIndexValue, unsigned indexCount,
                                   unsigned vertexCount, const void* pIndexData, bool index16,
                                   const void* pVertexData, unsigned vertexStride) {
  return pDevice->DrawIndexedPrimitiveUP(ConvertPrimitiveType(type), minIndexValue, indexCount, vertexCount,
                                         pIndexData, index16 ? D3DFMT_INDEX16 : D3DFMT_INDEX32,
                                         pVertexData, vertexStride);
}

HRESULT D3D9Backend::DrawUP(ERenderPrimitiveType type, unsigned primitiveCount,
                            const void* pVertexData, unsigned vertexStride) {
  return pDevice->DrawPrimitiveUP(ConvertPrimitiveType(type), primitiveCount, pVertexData, vertexStride);
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
} // namespace Render
