#include "stdafx.h"

#include "renderflagsconverter.h"
#include "renderbackend.h"   // seam этапа 2: GetBackend()/HTexture/HBuffer
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
unsigned int Texture::GetMipSubLevelsCount() const
{
  // Уровни спрашивает бэкенд (шаг 13.2): у другого бэкенда у pDXTexture нет
  // COM-метода GetLevelCount.
  IRenderBackend::BackendTextureInfo info;
  if (FAILED(GetBackend()->GetTextureInfo(MakeBackendHandle<HTexture>(Get(pDXTexture)), &info)))
    return 0;
  return info.levels;
}

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
  , ReallocateHandler(deviceLostHandlerPriority)
  , levels(0)
{
  ZeroMemory(&desc, sizeof(desc));
	if (!pTex)
		return;
  // Описание текстуры даёт бэкенд (шаг 13.2): под другим бэкендом у pTex нет
  // COM-методов, а D3DSURFACE_DESC клиенту нужен — по нему текстура
  // пересоздаётся после потери устройства. Для D3D9 значения берутся из того же
  // GetLevelDesc(0)/GetLevelCount, что читал клиент раньше.
  IRenderBackend::BackendTextureInfo info;
  const HRESULT hrInfo = GetBackend()->GetTextureInfo(MakeBackendHandle<HTexture>(pTex), &info);
  if (FAILED(hrInfo)) {
    NI_DX_THROW(hrInfo, "Texture2D: GetTextureInfo");
    return;
  }
  desc.Width  = info.width;
  desc.Height = info.height;
  if (!BackendFormatToD3D(info.format, &desc.Format))
    NI_ASSERT(false, NStr::StrFmt("Texture2D: формат %d вне словаря D3D9", (int)info.format));
  if (!BackendPoolUsageToD3D(info.pool, info.usage, &desc.Pool, &desc.Usage))
    NI_ASSERT(false, NStr::StrFmt("Texture2D: пул %d вне словаря D3D9", (int)info.pool));
  // Type = D3DRTYPE_TEXTURE — как у COM-описания. Это не косметика:
  // OnResourcesReallocate пересоздаёт только desc.Type == D3DRTYPE_SURFACE, т.е.
  // текстуры, созданные из COM-указателя, пересоздания не требуют (так и было).
  desc.Type               = D3DRTYPE_TEXTURE;
  desc.MultiSampleType    = D3DMULTISAMPLE_NONE;
  desc.MultiSampleQuality = 0;
  levels = info.levels;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Texture2D::Texture2D(D3DSURFACE_DESC const& desc_, HandlerPriority deviceLostHandlerPriority)
  : TextureTyped(0)
  , ReallocateHandler(deviceLostHandlerPriority)
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
      // Recreate с теми же параметрами: desc — D3D9-описание (замена на проектное
      // описание — отдельная работа этапа 3), в seam уходит нейтральная лексика.
      HTexture hTex;
      hr = GetBackend()->CreateTexture2D(desc.Width, desc.Height, levels,
                                         D3DUsageToBackend(desc.Usage),
                                         D3DFormatToBackend(desc.Format),
                                         D3DPoolToBackend(desc.Pool, desc.Usage), &hTex);
      pDirect3DTexture9 = static_cast<IDirect3DTexture9*>(hTex.p);
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
  // Загрузка бит — через seam (шаг 13.1/13.2): у Vulkan нет «приведённой»
  // памяти, шаг строки обязан сообщать бэкенд.
  void* data = 0;
  unsigned pitch = 0;
  const BackendRect r = MakeBackendRect(pRect->left, pRect->top, pRect->right, pRect->bottom);
  HRESULT hr = GetBackend()->LockTexture2D(MakeBackendHandle<HTexture>(GetDXTexture()), level, 0u, &r,
                                           lockType, &data, &pitch);
  NI_DX_THROW(hr, __FUNCTION__);

  if( SUCCEEDED(hr) )
    return LockedRect((int)pitch, (uchar*)data);
  else
    return LockedRect(0, 0);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
LockedRect Texture2D::LockRect(unsigned int level, ERenderLockType lockType)
{
  HRESULT hr = E_FAIL;
  void* data = 0;
  unsigned pitch = 0;
  if( !RENDER_DISABLED && GetDXTexture() ) {
    hr = GetBackend()->LockTexture2D(MakeBackendHandle<HTexture>(GetDXTexture()), level, 0u, 0,
                                     lockType, &data, &pitch);
    NI_DX_THROW(hr, __FUNCTION__);
  }

  if( SUCCEEDED(hr) )
    return LockedRect((int)pitch, (uchar*)data);
  else
	  return LockedRect(0, 0);
}

#pragma warning(pop)
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Texture2D::UnlockRect(unsigned int level)
{
  if(!RENDER_DISABLED) {
    HRESULT hr = GetBackend()->UnlockTexture2D(MakeBackendHandle<HTexture>(GetDXTexture()), level, 0u);
    NI_DX_THROW(hr, __FUNCTION__);
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
DXSurfaceRef Texture2D::GetSurface(unsigned int level) const
{
  // Поверхность уровня спрашиваем у бэкенда (шаг 13.4c): под NvrhiBackend в
  // pDXTexture лежит ручка бэкенда, GetSurfaceLevel() по ней = вызов по чужой
  // vtable. Владение — как у COM: умный указатель присоединяет ссылку без AddRef.
  HSurface surf;
  const HRESULT hr = GetBackend()->GetTextureSurface(MakeBackendHandle<HTexture>(GetDXTexture()), level, &surf);
  NI_DX_THROW(hr, __FUNCTION__);
	return DXSurfaceRef(static_cast<IDirect3DSurface9*>(surf.p), false);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// mip-цепочка (GenerateSubLevels_1/2) перенесена в d3d9backend.cpp — шаг 13.2:
// она построена на COM-поверхностях, а способ её построения обязан выбирать
// бэкенд, а не клиент.
void Texture::GenerateMipSubLevels_()
{
  if(!pDXTexture)
    return;

  const HRESULT hr = GetBackend()->GenerateMipmaps(MakeBackendHandle<HTexture>(Get(pDXTexture)));
  if (FAILED(hr))
    DebugTrace("Texture: GenerateMipmaps — НЕ УДАЛОСЬ 0x%08X", (unsigned)hr);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool Texture2D::Update(Texture2D* pTex) 
{
	HRESULT hr = GetBackend()->UpdateTexture(MakeBackendHandle<HTexture>(pTex->GetDXTexture()),
	                                        MakeBackendHandle<HTexture>(GetDXTexture()));
	NI_VERIFY( hr == D3D_OK, "can't update texture!", return false );
	return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Texture2D::OnResourcesRelease()
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

void Texture2D::OnResourcesReallocate()
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

  OnResourcesReallocate();
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


void TextureVtx::OnResourcesReallocate()
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
