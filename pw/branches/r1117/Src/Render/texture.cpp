#include "stdafx.h"

#include "renderflagsconverter.h"
#include "texture.h"
#include "renderresourcemanager.h"
#include "IConfigManager.h"
#include "ImmediateRenderer.h"

namespace Render
{

static bool s_msg_enable = false;
REGISTER_VAR("debug_txtr_create_message", s_msg_enable, STORAGE_NONE);

DECLARE_NULL_RENDER_FLAG

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
unsigned int Texture::GetMipSubLevelsCount() const { return pDXTexture->GetLevelCount(); }

inline void Texture::SetTexture(IDirect3DBaseTexture9 *_tex)
{
  if( pDXTexture )
    UnregisterDXPtr( Get(pDXTexture) );

  pDXTexture.Attach(_tex);
  RegisterDXPtr(_tex);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Texture2D::Texture2D(IDirect3DTexture9 *pTex, HandlerPriority deviceLostHandlerPriority)
  : TextureTyped(pTex)
  , DeviceLostHandler(deviceLostHandlerPriority)
  , levels(0)
{
  ZeroMemory(&desc, sizeof(desc));
	if (!pTex)
		return;
	GetDXTexture()->GetLevelDesc(0, &desc);
  levels = GetDXTexture()->GetLevelCount();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Texture2D::Texture2D(D3DSURFACE_DESC const& desc_, HandlerPriority deviceLostHandlerPriority)
  : TextureTyped(0)
  , DeviceLostHandler(deviceLostHandlerPriority)
  , desc(desc_)
  , levels(1)
{
  ASSERT(desc.Type == D3DRTYPE_SURFACE);

  if( (D3DUSAGE_AUTOGENMIPMAP & desc_.Usage) && Compatibility::IsRunnedUnderWine() )
  {
    levels = 0;
    desc.Usage &= ~D3DUSAGE_AUTOGENMIPMAP;
  }

  CreateInternal();
}

#define TEXTURE_RETRY_CREATE
#define TEXTURE_NUM_RETRIES 10

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Texture2D::CreateInternal()
{
  NI_ASSERT(!pDXTexture, "Lost Device was skipped");

  // recreate resource with old parameters
  IDirect3DTexture9* pDirect3DTexture9 = 0;
  if(!RENDER_DISABLED) {
    HRESULT hr = E_FAIL;
#ifdef TEXTURE_RETRY_CREATE
    for(int i = 0; i <= TEXTURE_NUM_RETRIES && FAILED(hr); ++i) {
#endif
      hr = GetDevice()->CreateTexture(desc.Width, desc.Height, levels, desc.Usage, desc.Format, desc.Pool, &pDirect3DTexture9, 0);
#ifdef TEXTURE_RETRY_CREATE
      if( SUCCEEDED(hr) )
        break;
      Sleep(50); // Give memory manager a chance to free some memory. 50 - arbitrary value
    }
#endif
    if( FAILED(hr) ) {
      if(D3DUSAGE_RENDERTARGET == desc.Usage)
        ShowErrorMessageAndTerminate(hr);
      else if(s_msg_enable) {
        MessageBox(NULL, "Couldn't create texture;\nensure all video memory is free", NULL,
          MB_ICONSTOP|MB_TASKMODAL|MB_SETFOREGROUND|MB_TOPMOST|MB_SERVICE_NOTIFICATION);
      }
    }
    NI_DX_WARN( hr, NStr::StrFmt("Texture2D::CreateInternal(), format %s, %s, size %dx%d, %d levels",
                                  D3DFormat2Str(desc.Format), D3DPool2Str(desc.Pool),
                                  desc.Width, desc.Height, levels) );
  }
  Texture::SetTexture(pDirect3DTexture9);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
LockedRect Texture2D::LockRect(unsigned int level, unsigned int left, unsigned int right, unsigned int top, unsigned int bottom, ERenderLockType lockType)
{
  RECT rect = {left, top, right, bottom};

	return LockRect(level, &rect, lockType);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma warning(push)
#pragma warning(disable: 4701) // potentially uninitialized local variable 'lockedRect' used

LockedRect Texture2D::LockRect(unsigned int level, RECT *pRect, ERenderLockType lockType)
{
	D3DLOCKED_RECT lockedRect;

	HRESULT hr = GetDXTexture()->LockRect( level, &lockedRect, pRect, ConvertRenderLockType(lockType) );
  NI_DX_THROW(hr, __FUNCTION__);

  if( SUCCEEDED(hr) )
    return LockedRect(lockedRect.Pitch, (uchar*)lockedRect.pBits);
  else
    return LockedRect(0, 0);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
LockedRect Texture2D::LockRect(unsigned int level, ERenderLockType lockType)
{
  D3DLOCKED_RECT lockedRect;

	HRESULT hr = E_FAIL;
  if( !RENDER_DISABLED && GetDXTexture() ) {
    hr = GetDXTexture()->LockRect( level, &lockedRect, NULL, ConvertRenderLockType(lockType) );
    NI_DX_THROW(hr, __FUNCTION__);
  }

  if( SUCCEEDED(hr) )
    return LockedRect(lockedRect.Pitch, (uchar*)lockedRect.pBits);
  else
	  return LockedRect(0, 0);
}

#pragma warning(pop)
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Texture2D::UnlockRect(unsigned int level)
{
  if(!RENDER_DISABLED) {
    HRESULT hr = GetDXTexture()->UnlockRect( level );
    NI_DX_THROW(hr, __FUNCTION__);
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
DXSurfaceRef Texture2D::GetSurface(unsigned int level) const
{
	IDirect3DSurface9* pSurf = 0;
	HRESULT hr = GetDXTexture()->GetSurfaceLevel(level, &pSurf);
  NI_DX_THROW(hr, __FUNCTION__);
	return DXSurfaceRef(pSurf, false);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void GenerateSubLevels_1(uint _mipLevels, IDirect3DTexture9* _pTex)
{
  IDirect3DDevice9* const pDevice = GetDevice();
  IDirect3DSurface9 *pSurf = 0;

  HRESULT hr = _pTex->GetSurfaceLevel(0, &pSurf);
  DXSurfaceRef pSrc(pSurf, false);
  NI_DX_THROW(hr, "GenerateSubLevels_1: failed to lock");

  for(uint n = 1; n < _mipLevels; ++n) {
    hr = _pTex->GetSurfaceLevel(n, &pSurf);
    NI_DX_THROW(hr, "GenerateSubLevels_1: failed to lock");

    DXSurfaceRef pDest(pSurf, false);
    hr = pDevice->StretchRect(Get(pSrc), 0, Get(pDest), 0, D3DTEXF_LINEAR);
    NI_DX_THROW(hr, "GenerateSubLevels_1: failed to stretch");

    Swap(pSrc, pDest);
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void GenerateSubLevels_2(uint _mipLevels, IDirect3DTexture9* _pTex)
{
  if(_mipLevels <= 1)
    return;

  IDirect3DDevice9* const pDevice = GetDevice();
  IDirect3DSurface9 *pSurf = 0;

  HRESULT hr = _pTex->GetSurfaceLevel(0, &pSurf);
  DXSurfaceRef pSrc(pSurf, false);
  NI_DX_THROW(hr, "GenerateSubLevels_2: failed to lock");

  D3DSURFACE_DESC desc;
  hr = _pTex->GetLevelDesc(1, &desc);
  hr = pDevice->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &pSurf, 0);
  DXSurfaceRef tmp(pSurf, false);
  NI_DX_THROW(hr, "GenerateSubLevels_2: failed to create temporary surface");

  RECT rect = {0};
  for(uint n = 1; n < _mipLevels; ++n) {
    hr = _pTex->GetLevelDesc(n, &desc);
    rect.right = desc.Width;
    rect.bottom = desc.Height;
    hr = pDevice->StretchRect(Get(pSrc), 0, Get(tmp), &rect, D3DTEXF_LINEAR);
    NI_DX_THROW(hr, "GenerateSubLevels_2: failed to stretch");

    hr = _pTex->GetSurfaceLevel(n, &pSurf);
    NI_DX_THROW(hr, "GenerateSubLevels_2: failed to lock");

    pSrc.Attach(pSurf);
    hr = pDevice->StretchRect( Get(tmp), &rect, Get(pSrc), 0, D3DTEXF_LINEAR);
    NI_DX_THROW(hr, "GenerateSubLevels_2: failed to stretch");
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Texture2D::GenerateMipSubLevels_()
{
  if(!pDXTexture)
    return;

  if( uint mipLevels = levels ? levels : pDXTexture->GetLevelCount() )
    if( Compatibility::IsRunnedUnderCrossOverWine() )
      GenerateSubLevels_1( mipLevels, GetDXTexture() );
    else
      GenerateSubLevels_2( mipLevels, GetDXTexture() );
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool Texture2D::Update(Texture2D* pTex) 
{
	HRESULT hr = GetDevice()->UpdateTexture(pTex->GetDXTexture(), GetDXTexture());
	NI_VERIFY( hr == D3D_OK, "can't update texture!", return false );
	return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Texture2D::OnDeviceLost()
{
  if (!pDXTexture)
    return;
  ASSERT(desc.Type == D3DRTYPE_SURFACE);

  // free VRAM resource
  if((desc.Pool == D3DPOOL_DEFAULT) && pDXTexture)
  {
    UnregisterDXPtr( Get(pDXTexture) );
    pDXTexture = 0;
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Texture2D::OnDeviceReset()
{
  if (desc.Type == D3DRTYPE_SURFACE && desc.Pool == D3DPOOL_DEFAULT)
    CreateInternal();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Texture2D::SetTexture( Texture2D const& theOther )
{
  pDXTexture = theOther.pDXTexture;
  desc = theOther.desc;
  levels = theOther.levels;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Текстура с построчными данными для вершинной стадии (PostFX-коэффициенты).
// Только vertex-texture путь: R2VB удалён (см. texture.h).
TextureVtx::TextureVtx(const Sizes& _sizes)
: pixelSize(8), sampler(UINT_MAX), sizes(_sizes)
{
  desc.Format = D3DFMT_A16B16G16R16F;
  desc.Type   = D3DRTYPE_TEXTURE;
  desc.Usage  = D3DUSAGE_RENDERTARGET;
  desc.Pool   = D3DPOOL_DEFAULT;
  desc.MultiSampleType    = D3DMULTISAMPLE_NONE;
  desc.MultiSampleQuality = 0;

  levels = 1;

  OnDeviceReset();
}


void TextureVtx::Bind( unsigned int _sampler )
{
  sampler = _sampler;
  GetStatesManager()->SetSampler(sampler, SamplerState::PRESET_CLAMP_POINT(), this);
}


void TextureVtx::UnBind()
{
  if(UINT_MAX != sampler) {
    SmartRenderer::UnBindTexture(sampler);
    sampler = UINT_MAX;
  }
}


void TextureVtx::OnDeviceReset()
{
  desc.Width  = sizes.tex.cx;
  desc.Height = sizes.tex.cy;
  CreateInternal();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TextureCube::TextureCube( IDirect3DCubeTexture9 *pTex ) : TextureTyped(pTex)
{
}

void TextureCube::SetTexture( TextureCube const& theOther )
{
  pDXTexture = theOther.pDXTexture;
}

}; // namespace Render
