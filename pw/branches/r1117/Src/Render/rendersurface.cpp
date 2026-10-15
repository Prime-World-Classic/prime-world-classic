#include "stdafx.h"

#include "rendersurface.h"
#include "renderflagsconverter.h"
#include "renderbackend.h"   // seam этапа 2: GetBackend()/HSurface/EBackend*

// pSurface == 0, desc == 0  =>  empty
// pSurface == 0, desc != 0  =>  waiting for OnResourcesReallocate()
// pSurface != 0, desc != 0  =>  ready

namespace Render
{

RenderSurface::~RenderSurface()
{
  if( pSurface && (D3DPOOL_MANAGED != desc.Pool) )
    UnregisterDXPtr( Get(pSurface) );
}

RenderSurface::RenderSurface()
{
  ZeroMemory(&desc, sizeof(desc));
}

RenderSurface::RenderSurface( IDirect3DSurface9* pSurface_ ) : pSurface( pSurface_, false )
{
  ZeroMemory(&desc, sizeof(desc));
  if (!pSurface)
    return;
  // Описание даёт бэкенд (шаг 13.2): под другим бэкендом у pSurface нет COM-
  // метода GetDesc, а D3DSURFACE_DESC нужен и для пересоздания после потери
  // устройства, и для учётных проверок (Pool). Мультисэмплирование seam не
  // описывает — MSAA-поверхности этим конструктором не создаются.
  IRenderBackend::BackendTextureInfo info;
  if (SUCCEEDED(GetBackend()->GetSurfaceInfo(MakeBackendHandle<HSurface>(Get(pSurface)), &info))) {
    desc.Width  = info.width;
    desc.Height = info.height;
    if (!BackendFormatToD3D(info.format, &desc.Format))
      NI_ASSERT(false, NStr::StrFmt("RenderSurface: формат %d вне словаря D3D9", (int)info.format));
    if (!BackendPoolUsageToD3D(info.pool, info.usage, &desc.Pool, &desc.Usage))
      NI_ASSERT(false, NStr::StrFmt("RenderSurface: пул %d вне словаря D3D9", (int)info.pool));
    desc.Type               = D3DRTYPE_SURFACE;
    desc.MultiSampleType    = D3DMULTISAMPLE_NONE;
    desc.MultiSampleQuality = 0;
  } else
    NI_ASSERT(false, "RenderSurface: GetSurfaceInfo не разобрано");
  ASSERT(desc.Type == D3DRTYPE_SURFACE);
}

RenderSurface::RenderSurface( D3DSURFACE_DESC const& desc_ ) : desc( desc_ )
{
  ASSERT(desc.Type == D3DRTYPE_SURFACE);
  CreateInternal();
}

void RenderSurface::Init( D3DSURFACE_DESC const& desc_ )
{
  ASSERT(desc_.Type == D3DRTYPE_SURFACE);
  desc = desc_;
  CreateInternal();
}

void RenderSurface::CreateInternal()
{
  ASSERT(!pSurface);
  IDirect3DSurface9* pDirect3DSurface = 0;
  HRESULT hr = E_INVALIDARG;
  const bool wasNotManaged = (D3DPOOL_MANAGED != desc.Pool);

  // Разделение «чем является поверхность» — за seam'ом (EBackendSurfaceKind):
  // у Vulkan/DX12 это одна текстура с usage-флагами, а не три фабрики.
  // Multisample не передаётся: в клиенте он D3DMULTISAMPLE_NONE (замер).
  HSurface hSurf;
  if( desc.Format == D3DFMT_D24S8 )
  {
    hr = GetBackend()->CreateSurface(ssDepthStencil, desc.Width, desc.Height,
                                     D3DFormatToBackend(desc.Format), BACKEND_POOL_DEFAULT, &hSurf);
    desc.Pool = D3DPOOL_DEFAULT;
  }
  else if( D3DUSAGE_RENDERTARGET == desc.Usage )
  {
    hr = GetBackend()->CreateSurface(ssRenderTarget, desc.Width, desc.Height,
                                     D3DFormatToBackend(desc.Format), BACKEND_POOL_DEFAULT, &hSurf);
    desc.Pool = D3DPOOL_DEFAULT;
  }
  else if( D3DPOOL_MANAGED != desc.Pool )
    hr = GetBackend()->CreateSurface(ssPlain, desc.Width, desc.Height,
                                     D3DFormatToBackend(desc.Format),
                                     D3DPoolToBackend(desc.Pool, desc.Usage), &hSurf);
  pDirect3DSurface = static_cast<IDirect3DSurface9*>(hSurf.p);

  if( D3D_OK == hr )
  {
    if( pSurface && wasNotManaged )
      UnregisterDXPtr( Get(pSurface) );

    pSurface.Attach(pDirect3DSurface);
    RegisterDXPtr(pDirect3DSurface);
  }
  else
    NI_ASSERT( false, NStr::StrFmt("RenderSurface::CreateInternal() Failed. code: 0x%08X, error: %s description: %s",
                                   hr, DXGetErrorStringA(hr), DXGetErrorDescriptionA(hr)) );
}

LockedRect RenderSurface::LockRect( const Rect& rect, ERenderLockType lockType )
{
	RECT d3drect;
	d3drect.bottom = rect.bottom;
	d3drect.top = rect.top;
	d3drect.left = rect.left;
	d3drect.right = rect.right;

	return LockRectInternal(&d3drect, lockType);
}

LockedRect RenderSurface::Lock( ERenderLockType lockType )
{
	return LockRectInternal(0, lockType );
}

void RenderSurface::Unlock()
{
  const HRESULT hr = GetBackend()->UnlockSurface(MakeBackendHandle<HSurface>(Get(pSurface)));
  NI_DX_THROW(hr, __FUNCTION__);
}

void RenderSurface::StretchTo( IDirect3DSurface9 *other, bool bLinear )
{
	HRESULT hr = GetBackend()->StretchRect(MakeBackendHandle<HSurface>(Get(pSurface)), 0,
	                                   MakeBackendHandle<HSurface>(other), 0,
	                                   bLinear ? IRenderBackend::bfLinear : IRenderBackend::bfPoint);
  NI_DX_THROW(hr, __FUNCTION__);
}

LockedRect RenderSurface::LockRectInternal( RECT* rect, ERenderLockType lockType )
{
  // Блокировка поверхности — через seam (шаг 13.2): у Vulkan нет приведённой
  // памяти, шаг строки сообщает бэкенд.
  void* data = 0;
  unsigned pitch = 0;
  const BackendRect r = rect ? MakeBackendRect(rect->left, rect->top, rect->right, rect->bottom)
                             : MakeBackendRect(0, 0, 0, 0);
  const HRESULT hr = GetBackend()->LockSurface(MakeBackendHandle<HSurface>(Get(pSurface)),
                                               rect ? &r : 0, lockType, &data, &pitch);
  NI_DX_THROW(hr, __FUNCTION__);

	LockedRect res;
	res.data = reinterpret_cast< unsigned char* >( data );
	res.pitch = (int)pitch;

	return res;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RenderSurface::OnResourcesRelease()
{
  if (!pSurface)
    return;
  ASSERT(desc.Type == D3DRTYPE_SURFACE);

  if( pSurface && (D3DPOOL_MANAGED != desc.Pool) )
    UnregisterDXPtr( Get(pSurface) );

  // free VRAM resource
  if(desc.Pool == D3DPOOL_DEFAULT)
    pSurface = 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RenderSurface::OnResourcesReallocate()
{
  // skip empty surfaces and the ones that not in VRAM
  if (desc.Type == D3DRTYPE_SURFACE && desc.Pool == D3DPOOL_DEFAULT)
    CreateInternal(); // note: may fail due to device lost again
}

} // namespace Render
