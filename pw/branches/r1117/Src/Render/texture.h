#pragma once

#include "rendersurface.h"
#include "DxIntrusivePtr.h"
#include "Reallocate.h"

namespace Render
{

void OnTextureDestruction(class Texture *tex);

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Texture : public CObjectBase
{
  OBJECT_BASIC_METHODS( Texture );

public:
  Texture() {}

  explicit Texture(IDirect3DBaseTexture9 *pTex) : pDXTexture(pTex, false)
  {
    RegisterDXPtr(pTex);
  }
  virtual ~Texture()
  {
    if( pDXTexture )
      UnregisterDXPtr( Get(pDXTexture) );
    OnTextureDestruction(this);
  }

	unsigned int GetMipSubLevelsCount() const;

  const DXBaseTextureRef& GetDXTexture() const { return pDXTexture; }
  void  GenerateMipSubLevels()
  {
    if( Compatibility::IsRunnedUnderWine() )
      GenerateMipSubLevels_(); // Wine did not implemented automipmaps yet
    else
      if( IDirect3DBaseTexture9* pTex = Get(GetDXTexture()) )
        pTex->GenerateMipSubLevels();
  }

protected:
  virtual void GenerateMipSubLevels_() {}
  void SetTexture(IDirect3DBaseTexture9 *_tex);

	DXBaseTextureRef pDXTexture;
};

template <typename T>
class TextureTyped : public Texture
{
public: 
	TextureTyped() {}
	TextureTyped(T *pDXTex) : Texture(pDXTex) {}

	T* GetDXTexture() const { return static_cast<T*>(Get(pDXTexture)); }
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Texture2D : public TextureTyped<IDirect3DTexture9>, public ReallocateHandler
{
  OBJECT_BASIC_METHODS( Texture2D );

public:
  typedef ReallocateWrapper<Texture2D> Wrapped;

  unsigned int GetWidth()  const { return desc.Width; }
	unsigned int GetHeight() const { return desc.Height; }
  D3DFORMAT    GetFormat() const { return desc.Format; }

	// Texture locking
  LockedRect LockRect(unsigned int level, unsigned int left, unsigned int right, unsigned int top, unsigned int bottom, ERenderLockType lockType);
  LockedRect LockRect(unsigned int level, RECT *pRect, ERenderLockType lockType);
	LockedRect LockRect(unsigned int level, ERenderLockType lockType);
	void UnlockRect(unsigned int level);

	// Texture updating
	bool Update(Texture2D *pTex);

	// Retrieving surface
	DXSurfaceRef GetSurface(unsigned int level) const;

  // Handle lost device
  virtual void OnResourcesRelease();
  virtual void OnResourcesReallocate();

  // Set new texture
  void SetTexture(Texture2D const& pOther);

protected:
  Texture2D() : levels(0) { ZeroMemory(&desc, sizeof(desc)); }
  explicit Texture2D(IDirect3DTexture9 *pTex, HandlerPriority deviceLostHandlerPriority = HANDLERPRIORITY_NORMAL);
  explicit Texture2D(D3DSURFACE_DESC const& desc_, HandlerPriority deviceLostHandlerPriority = HANDLERPRIORITY_NORMAL);

  void CreateInternal();

  virtual void GenerateMipSubLevels_();

  UINT levels;
  D3DSURFACE_DESC desc;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Текстура с построчными данными для вершинной стадии (PostFX-коэффициенты).
//
// Исторически имела два режима: ATI Render-to-VB (DMA-поверхность, читаемая
// через D3DRS_POINTSIZE-токены) и vertex texture. R2VB удалён: под Vulkan/DX12
// аналога нет, а именно он был единственным владельцем VS-слотов сэмплеров
// (см. Sampler::MAX_VS_SAMPLER_INDEX). Оставлен vertex-texture путь; для
// Vulkan он станет structured/typed buffer'ом.
class TextureVtx : public Texture2D
{
  OBJECT_METHODS(0x3D897500, TextureVtx);

public:
  typedef ReallocateWrapper<TextureVtx> Wrapped;

  struct Sizes
  {
    SIZE tex;

    Sizes() {}
    Sizes(UINT _width, UINT _heigth)
    {
      tex.cx = _width;
      tex.cy = _heigth;
    }
  };

  // WARNING! These methods are NOT virtual! Use typecasts and GetTypeId().
  void Bind(UINT _sampler);
  void UnBind();
  UINT GetPixelSize() const { return pixelSize; }

  virtual void OnResourcesReallocate();

protected:
  TextureVtx() { NI_ASSERT(FALSE, "Invalid TextureVtx constructor called!"); } // just to make OBJECT_METHODS happy
  explicit TextureVtx(const Sizes& _sizes);

private:
  UINT pixelSize;
  UINT sampler;
  Sizes sizes;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class TextureCube : public TextureTyped< IDirect3DCubeTexture9 >
{
public :
  explicit TextureCube( IDirect3DCubeTexture9 *pTex );

  // Set new texture
  void SetTexture(TextureCube const& pOther);
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ref types
typedef CObj<Texture> TextureRef;
typedef CObj<Texture2D> Texture2DRef;
typedef CObj<TextureVtx> TextureVtxRef;
typedef CObj<TextureCube> TextureCubeRef;

inline bool IsValidPtr(const Texture* _tex)
{
  return _tex && _tex->GetDXTexture();
}

}; // namespace Render
