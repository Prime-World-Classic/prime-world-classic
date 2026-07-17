#pragma once

#include "texture.h"
#if defined(PW_LINUX_NULL_RENDER)
#include "dxutils.h"
#else
#include "DxIntrusivePtr.h"
#endif

namespace NDb
{
  struct BaseUIMaterial;
}

namespace Render
{

class BaseMaterial;
_interface IFlashRenderer;
_interface IUITextureCache;


namespace ERenderWhat
{
  enum Enum
  {
    _2D,
    _3D
  };
}



namespace ETransformMode
{
  enum Enum
  {
    World,
    Billboard,
    AxisBillboard,
    DepthOnly
  };
};



typedef CTRect<float> UIRect;
typedef CTPoint<float> UIPoint;


struct UIQuad
{
  CVec2 tl;    // quad top left coordinate
  CVec2 br;    // quad right bottom coordinate
  CVec2 uv;    // texture1 top left
  CVec2 uvl;   // texture1 right bottom
  CVec2 uv2;   // texture2 top left
  CVec2 uvl2;  // texture2 right bottom

  bool ext;    // flag to enable extended features
  CVec2 pivot; // rotation and scaling pivot point
  float angle; // rotation angle in radians (counter-clockwise)
  float scale; // scaling factor

  UIQuad() {}

  UIQuad(	const UIPoint & _tl, const UIPoint & _br, const CVec2 & _uv, const CVec2 & _uvl, const CVec2 & _uv2, const CVec2 & _uvl2 )
    : uv( _uv ), uvl( _uvl ), uv2( _uv2 ), uvl2( _uvl2 ), ext( false )
  {
    tl.x = _tl.x;
    tl.y = _tl.y;
    br.x = _br.x;
    br.y = _br.y;
  }

  void SetExtParams( const UIPoint & _pivot, float _angle, float _scale )
  {
    ext = true;
    pivot.x = _pivot.x;
    pivot.y = _pivot.y;
    angle = _angle;
    scale = _scale;
  }
};



struct SMaterialParams
{
  Render::Color color0;
  float coef3;
  float coef4;

  SMaterialParams(): color0(255,255,255,255), coef3(0.f), coef4(1.f) {}
};



class UIRenderMaterial
{
public:
  UIRenderMaterial();
  UIRenderMaterial( const UIRenderMaterial& );
  UIRenderMaterial( const NDb::BaseUIMaterial* _material );
  UIRenderMaterial( const NDb::BaseUIMaterial* _material, void * _texturePoolId );
  ~UIRenderMaterial();

  void Release();

  UIRenderMaterial& operator = ( const UIRenderMaterial& );

  void CreateDefaultMaterial();
  void SetDBMaterial( const NDb::BaseUIMaterial * _material, void * _texturePoolId, bool forceReload = false );

  const NDb::BaseUIMaterial* GetDBMaterial() const;
  Render::BaseMaterial* GetRenderMaterial();

private:
  NDb::Ptr<NDb::BaseUIMaterial> dbMaterial;
  Render::BaseMaterial* renderMaterial;
  void * texturePoolId;
};



_interface IUIRenderer
{
  virtual bool Initialize() = 0;
  virtual void Release() = 0;

  virtual void StartFrame() = 0;

  virtual void PushCrop( const UIRect & cropRect ) = 0;
  virtual void PushNoCrop() = 0;
  virtual void PopCrop() = 0;

  virtual void AddQuad( UIQuad & _quad, Render::BaseMaterial* _renderMaterial, const SMaterialParams & params ) = 0;
  virtual void BeginFlashParts( int _startFlashElement ) = 0;
  virtual void EndFlashParts( int _lastFlashElement ) = 0;

  virtual void BeginText() = 0;
  virtual void AddTextQuad( UIQuad & _quad, const SMaterialParams & params ) = 0;
  virtual void EndText( Render::BaseMaterial* _renderMaterial ) = 0;

  virtual void SetViewMatrices( const SHMatrix & view, const SHMatrix & projection ) = 0;
  virtual void SetWorldMatrix( const SHMatrix & world, ETransformMode::Enum transformMode, const CVec3 & pivot, float depthBias ) = 0;
  virtual void ResetWorldMatrix() = 0;
  virtual void GetBillboardMatrix( SHMatrix * pCombined, const SHMatrix & world, ETransformMode::Enum transformMode, const CVec3 & pivot, float depthBias ) = 0;
  virtual void GetRay( CVec3 * pOrigin, CVec3 * pDir, int sx, int sy ) = 0;
  virtual float CalcDepth( const CVec3 & point ) = 0;

  virtual void SetResolutionCoefs( const float x, const float y, const float widthScale, const float heightScale ) = 0;
  virtual const CVec4& GetResolutionCoefs() const = 0;

  virtual void SetFontTextureSize( const int width, const int height ) = 0;

  virtual void BeginQueue() = 0;
  virtual void EndQueue() = 0;

  virtual void Render( ERenderWhat::Enum what, const Render::Texture2DRef& pMainRT0, const Render::Texture2DRef& pMainRT0Copy ) = 0;

  virtual void PrepareRender() = 0;
  virtual void PrepareRenderFromFlash() = 0;
  virtual void RenderPart( int _partID, ERenderWhat::Enum what, bool alphaTest ) = 0;
  virtual BaseMaterial* GetPartMaterial( int _partID, ERenderWhat::Enum what ) = 0;

  virtual void SetSaturation( float _val, const CVec4& _color, bool saturate ) = 0;
  virtual bool ForbidSaturation( bool _forbid) = 0;

  virtual IFlashRenderer* GetFlashRenderer() = 0;
  virtual IUITextureCache* GetTextureCache() = 0;
};

// $TODO get rid of singleton nature
IUIRenderer* GetUIRenderer();

#if defined(PW_LINUX_NULL_RENDER) && defined(PW_LINUX_OPENGL_BOOTSTRAP)
struct LinuxOpenGLUiRendererStats
{
  unsigned int queued2DQuads;
  unsigned int queued2DTextQuads;
  unsigned int queuedTextured2DQuads;
  unsigned int queued3DQuads;
  unsigned int rendered2DQuads;
  unsigned int rendered2DTextQuads;
  unsigned int renderedTextured2DQuads;
  unsigned int rendered3DQuads;
  unsigned int missingOpenGLTexture2DQuads;
  unsigned int cropRejectedQuads;
  unsigned int render2DCalls;
  unsigned int render3DCalls;
  unsigned int renderedFlashParts;
  unsigned int renderedFlashCommands;
  unsigned int renderedFlashScissorCommands;
  unsigned int renderedFlashMaskCommands;
  unsigned int renderedFlashBlendCommands;
  unsigned int renderedFlashLineCommands;
  unsigned int renderedFlashLineVertices;
  unsigned int renderedFlashTexturedCommands;
  unsigned int renderedFlashRepeatCommands;
  unsigned int renderedFlashClampCommands;
  unsigned int renderedFlashScale9Commands;

  LinuxOpenGLUiRendererStats()
    : queued2DQuads(0)
    , queued2DTextQuads(0)
    , queuedTextured2DQuads(0)
    , queued3DQuads(0)
    , rendered2DQuads(0)
    , rendered2DTextQuads(0)
    , renderedTextured2DQuads(0)
    , rendered3DQuads(0)
    , missingOpenGLTexture2DQuads(0)
    , cropRejectedQuads(0)
    , render2DCalls(0)
    , render3DCalls(0)
    , renderedFlashParts(0)
    , renderedFlashCommands(0)
    , renderedFlashScissorCommands(0)
    , renderedFlashMaskCommands(0)
    , renderedFlashBlendCommands(0)
    , renderedFlashLineCommands(0)
    , renderedFlashLineVertices(0)
    , renderedFlashTexturedCommands(0)
    , renderedFlashRepeatCommands(0)
    , renderedFlashClampCommands(0)
    , renderedFlashScale9Commands(0)
  {
  }
};

const LinuxOpenGLUiRendererStats& GetLinuxOpenGLUiRendererStats();
void ResetLinuxOpenGLUiRendererStats();
void AddLinuxOpenGLUiRendererFlashStats(unsigned int parts, unsigned int commands, unsigned int scissorCommands, unsigned int maskCommands, unsigned int blendCommands, unsigned int lineCommands, unsigned int lineVertices, unsigned int texturedCommands, unsigned int repeatCommands, unsigned int clampCommands, unsigned int scale9Commands);
void SetLinuxOpenGLUiRendererFlashTextStyle(int partID, const Texture2DRef& texture, bool drawBevel, const Color& bevelColor);
#endif

} // namespace Render
