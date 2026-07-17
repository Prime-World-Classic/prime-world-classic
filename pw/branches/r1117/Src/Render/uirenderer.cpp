#include "stdafx.h"

#if defined(PW_LINUX_NULL_RENDER)

#include "uirenderer.h"
#include "DBRenderResources.h"
#include "FlashRenderer.h"
#include "MaterialSpec.h"
#include "TextureManager.h"

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
#include "../System/MainFrame.h"
#include <GL/gl.h>
#endif

namespace Render
{

namespace
{

static const UIRect noCrop(-1, -1, -1, -1);
static const unsigned int LINUX_UI_QUAD_MAX_COUNT = 20000;
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
LinuxOpenGLUiRendererStats g_linuxOpenGLUiRendererStats;
#endif

bool FillLinuxUIRenderMaterialSampler(
  const NDb::Sampler& dbSampler,
  Render::Sampler* sampler,
  void* texturePool)
{
  if (!sampler)
    return false;

  sampler->SetSamplerState(dbSampler.samplerState);
  sampler->SetTexture(0);

  if (!IsValid(dbSampler.texture))
    return false;

  const NDb::Texture* texture = dynamic_cast<const NDb::Texture*>(dbSampler.texture.GetPtr());
  if (!texture || texture->textureFileName.empty())
    return false;

  const Render::Texture2DRef loadedTexture =
    Render::LoadTexture2DIntoPool(*texture, false, texturePool);
  sampler->SetTexture(loadedTexture.GetPtr());
  return sampler->GetTexture();
}

bool FillLinuxUIRenderMaterialSamplerWithFallback(
  const NDb::Sampler& primarySampler,
  const NDb::Sampler& fallbackSampler,
  Render::Sampler* sampler,
  void* texturePool)
{
  if (FillLinuxUIRenderMaterialSampler(primarySampler, sampler, texturePool))
    return true;

  return FillLinuxUIRenderMaterialSampler(fallbackSampler, sampler, texturePool);
}

class LinuxUIRenderMaterial : public BaseMaterial
{
public:
  LinuxUIRenderMaterial()
    : BaseMaterial(0, 0, -1)
    , useDiffuse(NDb::BOOLEANPIN_PRESENT)
  {
  }

  void SetDBMaterial(const NDb::BaseUIMaterial* material, void* texturePool)
  {
    pDbMaterial = material;
    diffuseMap.SetTexture(0);
    useDiffuse = NDb::BOOLEANPIN_NONE;

    if (!material)
      return;

    Render::Material::FillMaterial(material, texturePool, false);

    if (const NDb::UIBaseMaterial* uiMaterial = dynamic_cast<const NDb::UIBaseMaterial*>(material))
    {
      FillLinuxUIRenderMaterialSampler(uiMaterial->DiffuseMap, &diffuseMap, texturePool);
      useDiffuse = uiMaterial->UseDiffuse;
      return;
    }

    if (const NDb::UIButtonMaterial* buttonMaterial = dynamic_cast<const NDb::UIButtonMaterial*>(material))
    {
      if (buttonMaterial->UseDiffusePin != NDb::BOOLEANPIN_NONE)
      {
        FillLinuxUIRenderMaterialSamplerWithFallback(
          buttonMaterial->DiffuseMap,
          buttonMaterial->BackgroundMap,
          &diffuseMap,
          texturePool);
      }
      else
      {
        FillLinuxUIRenderMaterialSampler(buttonMaterial->BackgroundMap, &diffuseMap, texturePool);
      }

      useDiffuse = diffuseMap.GetTexture() ? NDb::BOOLEANPIN_PRESENT : NDb::BOOLEANPIN_NONE;
      return;
    }

    if (const NDb::UIGlassMaterial* glassMaterial = dynamic_cast<const NDb::UIGlassMaterial*>(material))
    {
      FillLinuxUIRenderMaterialSamplerWithFallback(
        glassMaterial->DiffuseMap,
        glassMaterial->BackgroundMap,
        &diffuseMap,
        texturePool);
      useDiffuse = diffuseMap.GetTexture() ? NDb::BOOLEANPIN_PRESENT : NDb::BOOLEANPIN_NONE;
    }
  }

  virtual void PrepareRenderer()
  {
  }

  virtual Render::Sampler* GetDiffuseMap()
  {
    return &diffuseMap;
  }

  virtual const Render::Sampler* GetDiffuseMap() const
  {
    return &diffuseMap;
  }

  bool IsDiffuseEnabled() const
  {
    return useDiffuse != NDb::BOOLEANPIN_NONE;
  }

  void SetUseDiffuse(const NDb::BooleanPin value)
  {
    useDiffuse = value;
  }

  virtual const NDb::Material* GetDBMaterial() const
  {
    return pDbMaterial.GetPtr();
  }

private:
  NDb::Ptr<NDb::BaseUIMaterial> pDbMaterial;
  Render::Sampler diffuseMap;
  NDb::BooleanPin useDiffuse;
};

struct LinuxQueuedUIQuad
{
  UIQuad quad;
  Render::Color color;
  Render::Texture2DRef diffuseTexture;
  bool text;
  bool flashText;

  LinuxQueuedUIQuad()
    : quad()
    , color()
    , diffuseTexture()
    , text(false)
    , flashText(false)
  {
  }

  LinuxQueuedUIQuad(
    const UIQuad& _quad,
    const Render::Color& _color,
    const Render::Texture2DRef& _diffuseTexture,
    bool _text)
    : quad(_quad)
    , color(_color)
    , diffuseTexture(_diffuseTexture)
    , text(_text)
    , flashText(false)
  {
  }
};

// Minimal Linux-side UI part metadata used when Flash asks to replay text quads
// at a specific point in the SWF command stream.
struct LinuxQueuedRenderPart
{
  unsigned int firstQuad;
  unsigned int quadCount;
  bool drawBevel;
  Render::Color bevelColor;

  LinuxQueuedRenderPart()
    : firstQuad(0)
    , quadCount(0)
    , drawBevel(false)
    , bevelColor(0, 0, 0, 255)
  {
  }

  LinuxQueuedRenderPart(unsigned int _firstQuad, unsigned int _quadCount)
    : firstQuad(_firstQuad)
    , quadCount(_quadCount)
    , drawBevel(false)
    , bevelColor(0, 0, 0, 255)
  {
  }
};

struct LinuxQueuedFlashPart
{
  int firstElement;
  int lastElement;

  LinuxQueuedFlashPart()
    : firstElement(0)
    , lastElement(-1)
  {
  }

  LinuxQueuedFlashPart(int _firstElement, int _lastElement)
    : firstElement(_firstElement)
    , lastElement(_lastElement)
  {
  }
};

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
float ResolveLinuxUIRenderSurfaceSize(float coef, int viewportSize)
{
  if (coef > 0.00001f && coef < 1.0f)
    return 2.0f / coef;

  return static_cast<float>(viewportSize > 0 ? viewportSize : 1);
}

unsigned char LimitLinuxUIAlpha(unsigned char alpha, bool text)
{
  if (alpha == 0)
    return 0;

  const unsigned char cap = text ? 112 : 48;
  const unsigned char floorValue = text ? 40 : 18;
  unsigned char result = alpha < cap ? alpha : cap;
  return result < floorValue ? floorValue : result;
}

Render::Color ResolveLinuxUIFallbackColor(const Render::Color& color, bool text, bool textured)
{
  Render::Color result(color);
  if (textured)
    return result;

  if (!text && result.R > 238 && result.G > 238 && result.B > 238)
  {
    result.R = 82;
    result.G = 134;
    result.B = 162;
  }
  result.A = LimitLinuxUIAlpha(result.A, text);
  return result;
}

Render::Texture2DRef ResolveLinuxUITexture(Render::BaseMaterial* renderMaterial)
{
  if (!renderMaterial)
    return Render::Texture2DRef();

  const LinuxUIRenderMaterial* linuxMaterial = dynamic_cast<const LinuxUIRenderMaterial*>(renderMaterial);
  if (linuxMaterial && !linuxMaterial->IsDiffuseEnabled())
    return Render::Texture2DRef();

  const Render::Sampler* diffuseSampler = renderMaterial->GetDiffuseMap();
  if (!diffuseSampler || !diffuseSampler->GetTexture())
    return Render::Texture2DRef();

  return Render::Texture2DRef(
    dynamic_cast<Render::Texture2D*>(diffuseSampler->GetTexture().GetPtr()));
}
#endif

bool CropLinuxUIQuad(UIQuad* quad, const UIRect& cropRect)
{
  if (!quad)
    return false;

  const float originalLeft = quad->tl.x;
  const float originalTop = quad->tl.y;
  const float originalRight = quad->br.x;
  const float originalBottom = quad->br.y;
  const float originalWidth = originalRight - originalLeft;
  const float originalHeight = originalBottom - originalTop;
  if (originalWidth <= 0.0f || originalHeight <= 0.0f)
    return false;

  UIRect croppedRect(originalLeft, originalTop, originalRight, originalBottom);
  croppedRect.Intersect(cropRect);

  if (croppedRect.IsEmpty())
    return false;

  const float u1 = (croppedRect.x1 - originalLeft) / originalWidth;
  const float u2 = (croppedRect.x2 - originalLeft) / originalWidth;
  const float v1 = (croppedRect.y1 - originalTop) / originalHeight;
  const float v2 = (croppedRect.y2 - originalTop) / originalHeight;
  const CVec2 uv(quad->uv);
  const CVec2 uvl(quad->uvl);
  const CVec2 uv2(quad->uv2);
  const CVec2 uvl2(quad->uvl2);

  quad->tl.x = croppedRect.x1;
  quad->tl.y = croppedRect.y1;
  quad->br.x = croppedRect.x2;
  quad->br.y = croppedRect.y2;
  quad->uv.x = uv.x + (uvl.x - uv.x) * u1;
  quad->uv.y = uv.y + (uvl.y - uv.y) * v1;
  quad->uvl.x = uv.x + (uvl.x - uv.x) * u2;
  quad->uvl.y = uv.y + (uvl.y - uv.y) * v2;
  quad->uv2.x = uv2.x + (uvl2.x - uv2.x) * u1;
  quad->uv2.y = uv2.y + (uvl2.y - uv2.y) * v1;
  quad->uvl2.x = uv2.x + (uvl2.x - uv2.x) * u2;
  quad->uvl2.y = uv2.y + (uvl2.y - uv2.y) * v2;
  return true;
}

void TransformLinuxUIQuadPoint(float* x, float* y, const CVec2& pivot, float ksn, float kcs)
{
  const float px = *x - pivot.x;
  const float py = *y - pivot.y;
  *x = pivot.x + px * kcs + py * ksn;
  *y = pivot.y - px * ksn + py * kcs;
}

void BuildLinuxUIQuadPoints(const UIQuad& quad, float points[4][2])
{
  points[0][0] = quad.tl.x;
  points[0][1] = quad.tl.y;
  points[1][0] = quad.br.x;
  points[1][1] = quad.tl.y;
  points[2][0] = quad.br.x;
  points[2][1] = quad.br.y;
  points[3][0] = quad.tl.x;
  points[3][1] = quad.br.y;

  if (!quad.ext)
    return;

  const float ksn = sinf(quad.angle) * quad.scale;
  const float kcs = cosf(quad.angle) * quad.scale;
  for (int i = 0; i < 4; ++i)
    TransformLinuxUIQuadPoint(&points[i][0], &points[i][1], quad.pivot, ksn, kcs);
}

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
// First-pass Linux UI proof: draw queued geometry, using real UI textures when available.
void RenderLinuxOpenGLUIQuads(
  const vector<LinuxQueuedUIQuad>& quads,
  const CVec4& resolutionCoefs,
  unsigned int firstQuad,
  unsigned int quadCount,
  bool skipFlashText,
  const Render::Color* overrideColor,
  float offsetX,
  float offsetY)
{
  if (quads.empty() || firstQuad >= quads.size() || quadCount == 0 || !NMainFrame::MakeOpenGLContextCurrent())
    return;

  unsigned int lastQuad = firstQuad + quadCount;
  if (lastQuad > quads.size() || lastQuad < firstQuad)
    lastQuad = quads.size();

  GLint viewport[4] = { 0, 0, 0, 0 };
  glGetIntegerv(GL_VIEWPORT, viewport);
  if (viewport[2] <= 0 || viewport[3] <= 0)
    return;

  glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_TRANSFORM_BIT | GL_TEXTURE_BIT);

  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glDisable(GL_LIGHTING);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  const float surfaceWidth = ResolveLinuxUIRenderSurfaceSize(resolutionCoefs.x, viewport[2]);
  const float surfaceHeight = ResolveLinuxUIRenderSurfaceSize(resolutionCoefs.y, viewport[3]);

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0.0, surfaceWidth, surfaceHeight, 0.0, -1.0, 1.0);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  for (unsigned int i = firstQuad; i < lastQuad; ++i)
  {
    const LinuxQueuedUIQuad& queuedQuad = quads[i];
    if (skipFlashText && queuedQuad.flashText)
      continue;

    if (queuedQuad.diffuseTexture)
      queuedQuad.diffuseTexture->EnsureOpenGLTexture();

    const unsigned int openGLTexture = queuedQuad.diffuseTexture ?
      queuedQuad.diffuseTexture->GetOpenGLTexture() :
      0U;

    if (openGLTexture)
    {
      glEnable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, openGLTexture);
    }
    else
    {
      if (queuedQuad.diffuseTexture)
        ++g_linuxOpenGLUiRendererStats.missingOpenGLTexture2DQuads;
      glBindTexture(GL_TEXTURE_2D, 0);
      glDisable(GL_TEXTURE_2D);
    }

    float points[4][2];
    BuildLinuxUIQuadPoints(queuedQuad.quad, points);
    for (int point = 0; point < 4; ++point)
    {
      points[point][0] += offsetX;
      points[point][1] += offsetY;
    }

    const Render::Color& drawColor = overrideColor ? *overrideColor : queuedQuad.color;
    glColor4ub(drawColor.R, drawColor.G, drawColor.B, drawColor.A);
    glBegin(GL_QUADS);
    if (openGLTexture)
    {
      glTexCoord2f(queuedQuad.quad.uv.x, queuedQuad.quad.uv.y);
      glVertex2f(points[0][0], points[0][1]);
      glTexCoord2f(queuedQuad.quad.uvl.x, queuedQuad.quad.uv.y);
      glVertex2f(points[1][0], points[1][1]);
      glTexCoord2f(queuedQuad.quad.uvl.x, queuedQuad.quad.uvl.y);
      glVertex2f(points[2][0], points[2][1]);
      glTexCoord2f(queuedQuad.quad.uv.x, queuedQuad.quad.uvl.y);
      glVertex2f(points[3][0], points[3][1]);
      ++g_linuxOpenGLUiRendererStats.renderedTextured2DQuads;
    }
    else
    {
      for (int point = 0; point < 4; ++point)
        glVertex2f(points[point][0], points[point][1]);
    }
    glEnd();
    ++g_linuxOpenGLUiRendererStats.rendered2DQuads;
    if (queuedQuad.text)
      ++g_linuxOpenGLUiRendererStats.rendered2DTextQuads;
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);

  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();

  glPopAttrib();
}
#endif

class NullUIRenderer : public IUIRenderer
{
public:
  NullUIRenderer()
    : initialized(false)
    , resolutionCoefs(1.0f, 1.0f, 1.0f, 1.0f)
    , forbidSaturation(false)
    , textStarted(false)
    , textFirstQuad(0)
    , queueRendered(false)
    , queueOverflowed(false)
    , currentFlashPart(-1)
  {
    quads.reserve(1024);
    flashParts.reserve(32);
  }

  virtual bool Initialize()
  {
    if (!flashRenderer)
      flashRenderer = new FlashRenderer();
    if (!flashRenderer->Initialize())
      return false;

    initialized = true;
    StartFrame();
    return true;
  }

  virtual void Release()
  {
    initialized = false;
    quads.clear();
    cropRects.clear();
    textStarted = false;
    textFirstQuad = 0;
    queueRendered = false;
    queueOverflowed = false;
    currentFlashPart = -1;
    flashParts.clear();
    renderParts.clear();
    if (flashRenderer)
    {
      flashRenderer->Release();
      flashRenderer = 0;
    }
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
    ResetLinuxOpenGLUiRendererStats();
#endif
  }

  virtual void StartFrame()
  {
    quads.clear();
    cropRects.clear();
    textStarted = false;
    textFirstQuad = 0;
    queueRendered = false;
    queueOverflowed = false;
    currentFlashPart = -1;
    flashParts.clear();
    renderParts.clear();
    if (flashRenderer)
      flashRenderer->StartFrame();
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
    ResetLinuxOpenGLUiRendererStats();
#endif
  }

  virtual void PushCrop(const UIRect& cropRect)
  {
    cropRects.push_back(cropRect);
  }

  virtual void PushNoCrop()
  {
    cropRects.push_back(noCrop);
  }

  virtual void PopCrop()
  {
    if (!cropRects.empty())
      cropRects.pop_back();
  }

  virtual void AddQuad(UIQuad& quad, Render::BaseMaterial* renderMaterial, const SMaterialParams& params)
  {
    QueueQuad(quad, params.color0, false, renderMaterial);
  }

  virtual void BeginFlashParts(int startFlashElement)
  {
    currentFlashPart = static_cast<int>(flashParts.size());
    flashParts.push_back(LinuxQueuedFlashPart(startFlashElement, -1));
  }

  virtual void EndFlashParts(int lastFlashElement)
  {
    if (currentFlashPart < 0 || currentFlashPart >= static_cast<int>(flashParts.size()))
      return;

    flashParts[currentFlashPart].lastElement = lastFlashElement;
    currentFlashPart = -1;
    queueRendered = false;
  }

  virtual void BeginText()
  {
    textStarted = true;
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
    textFirstQuad = quads.size();
#endif
  }

  virtual void AddTextQuad(UIQuad& quad, const SMaterialParams& params)
  {
    if (!textStarted)
      return;

    quad.ext = false;
    QueueQuad(quad, params.color0, true, 0);
  }

  virtual void EndText(Render::BaseMaterial* renderMaterial)
  {
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
    if (!textStarted)
    {
      (void)renderMaterial;
      return;
    }

    const unsigned int textLastQuad = quads.size();
    const bool hasTextQuads = textFirstQuad < textLastQuad;
    const Render::Texture2DRef textTexture = ResolveLinuxUITexture(renderMaterial);
    if (textTexture)
    {
      for (unsigned int i = textFirstQuad; i < textLastQuad; ++i)
      {
        if (!quads[i].text || quads[i].diffuseTexture)
          continue;

        quads[i].diffuseTexture = textTexture;
        ++g_linuxOpenGLUiRendererStats.queuedTextured2DQuads;
      }
      queueRendered = false;
    }

    if (hasTextQuads && currentFlashPart >= 0)
    {
      for (unsigned int i = textFirstQuad; i < textLastQuad; ++i)
        quads[i].flashText = true;

      renderParts.push_back(LinuxQueuedRenderPart(textFirstQuad, textLastQuad - textFirstQuad));
      if (flashRenderer)
        flashRenderer->RenderText(static_cast<int>(renderParts.size() - 1));
      queueRendered = false;
    }
#else
    (void)renderMaterial;
#endif
    textStarted = false;
  }

  virtual void SetViewMatrices(const SHMatrix& view, const SHMatrix& projection)
  {
    (void)view;
    (void)projection;
  }

  virtual void SetWorldMatrix(const SHMatrix& world, ETransformMode::Enum transformMode, const CVec3& pivot, float depthBias)
  {
    (void)world;
    (void)transformMode;
    (void)pivot;
    (void)depthBias;
  }

  virtual void ResetWorldMatrix() {}

  virtual void GetBillboardMatrix(SHMatrix* pCombined, const SHMatrix& world, ETransformMode::Enum transformMode, const CVec3& pivot, float depthBias)
  {
    (void)transformMode;
    (void)pivot;
    (void)depthBias;
    if (pCombined)
      *pCombined = world;
  }

  virtual void GetRay(CVec3* pOrigin, CVec3* pDir, int sx, int sy)
  {
    (void)sx;
    (void)sy;
    if (pOrigin)
      *pOrigin = CVec3(0.0f, 0.0f, 0.0f);
    if (pDir)
      *pDir = CVec3(0.0f, 0.0f, 1.0f);
  }

  virtual float CalcDepth(const CVec3& point)
  {
    return point.z;
  }

  virtual void SetResolutionCoefs(const float x, const float y, const float widthScale, const float heightScale)
  {
    resolutionCoefs.Set(x, y, widthScale, heightScale);
    if (flashRenderer)
      flashRenderer->SetResolutionCoefs(x, y, widthScale, heightScale);
  }

  virtual const CVec4& GetResolutionCoefs() const
  {
    return resolutionCoefs;
  }

  virtual void SetFontTextureSize(const int width, const int height)
  {
    (void)width;
    (void)height;
  }

  virtual void BeginQueue()
  {
    StartFrame();
    if (flashRenderer)
      flashRenderer->BeginQueue();
  }

  virtual void EndQueue()
  {
    if (flashRenderer)
      flashRenderer->EndQueue();
  }

  virtual void Render(ERenderWhat::Enum what, const Render::Texture2DRef& pMainRT0, const Render::Texture2DRef& pMainRT0Copy)
  {
    (void)pMainRT0;
    (void)pMainRT0Copy;

    if (what != ERenderWhat::_2D || !initialized || queueRendered)
      return;

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
    ++g_linuxOpenGLUiRendererStats.render2DCalls;
    RenderLinuxOpenGLUIQuads(quads, resolutionCoefs, 0, quads.size(), true, 0, 0.0f, 0.0f);
    if (flashRenderer)
    {
      for (unsigned int i = 0; i < flashParts.size(); ++i)
      {
        if (flashParts[i].lastElement >= flashParts[i].firstElement)
          flashRenderer->Render(flashParts[i].firstElement, flashParts[i].lastElement, pMainRT0, pMainRT0Copy);
      }
    }
    queueRendered = true;
#else
    (void)what;
#endif
  }

  virtual void PrepareRender() {}
  virtual void PrepareRenderFromFlash() {}
  virtual void RenderPart(int partID, ERenderWhat::Enum what, bool alphaTest)
  {
    (void)alphaTest;
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
    if (what != ERenderWhat::_2D || partID < 0 || partID >= static_cast<int>(renderParts.size()))
      return;

    const LinuxQueuedRenderPart& part = renderParts[partID];
    if (part.drawBevel)
    {
      RenderLinuxOpenGLUIQuads(quads, resolutionCoefs, part.firstQuad, part.quadCount, false, &part.bevelColor, -1.0f, 0.0f);
      RenderLinuxOpenGLUIQuads(quads, resolutionCoefs, part.firstQuad, part.quadCount, false, &part.bevelColor, 1.0f, 0.0f);
      RenderLinuxOpenGLUIQuads(quads, resolutionCoefs, part.firstQuad, part.quadCount, false, &part.bevelColor, 0.0f, -1.0f);
      RenderLinuxOpenGLUIQuads(quads, resolutionCoefs, part.firstQuad, part.quadCount, false, &part.bevelColor, 0.0f, 1.0f);
    }
    RenderLinuxOpenGLUIQuads(quads, resolutionCoefs, part.firstQuad, part.quadCount, false, 0, 0.0f, 0.0f);
#else
    (void)partID;
    (void)what;
#endif
  }

  virtual BaseMaterial* GetPartMaterial(int partID, ERenderWhat::Enum what)
  {
    (void)partID;
    (void)what;
    return 0;
  }

  void SetFlashTextStyle(int partID, const Texture2DRef& texture, bool drawBevel, const Render::Color& bevelColor)
  {
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
    if (partID < 0 || partID >= static_cast<int>(renderParts.size()))
      return;

    LinuxQueuedRenderPart& part = renderParts[partID];
    part.drawBevel = drawBevel;
    part.bevelColor = bevelColor;

    unsigned int lastQuad = part.firstQuad + part.quadCount;
    if (lastQuad > quads.size() || lastQuad < part.firstQuad)
      lastQuad = quads.size();

    if (texture)
    {
      for (unsigned int i = part.firstQuad; i < lastQuad; ++i)
      {
        if (!quads[i].text || !quads[i].flashText || quads[i].diffuseTexture)
          continue;

        quads[i].diffuseTexture = texture;
        ++g_linuxOpenGLUiRendererStats.queuedTextured2DQuads;
      }
    }
    queueRendered = false;
#else
    (void)partID;
    (void)texture;
    (void)drawBevel;
    (void)bevelColor;
#endif
  }

  virtual void SetSaturation(float val, const CVec4& color, bool saturate)
  {
    (void)val;
    (void)color;
    (void)saturate;
  }

  virtual bool ForbidSaturation(bool forbid)
  {
    const bool previous = forbidSaturation;
    forbidSaturation = forbid;
    return previous;
  }

  virtual IFlashRenderer* GetFlashRenderer()
  {
    return flashRenderer;
  }

  virtual IUITextureCache* GetTextureCache()
  {
    return 0;
  }

private:
  void QueueQuad(UIQuad quad, const Render::Color& color, bool text, Render::BaseMaterial* renderMaterial)
  {
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
    if (!initialized)
      return;

    if (!quad.ext && !cropRects.empty() && !cropRects.back().IsSame(noCrop))
      if (!CropLinuxUIQuad(&quad, cropRects.back()))
      {
        ++g_linuxOpenGLUiRendererStats.cropRejectedQuads;
        return;
      }

    if (quad.br.x <= quad.tl.x || quad.br.y <= quad.tl.y)
    {
      ++g_linuxOpenGLUiRendererStats.cropRejectedQuads;
      return;
    }

    if (quads.size() >= LINUX_UI_QUAD_MAX_COUNT)
    {
      queueOverflowed = true;
      return;
    }

    const Render::Texture2DRef diffuseTexture = text ?
      Render::Texture2DRef() :
      ResolveLinuxUITexture(renderMaterial);
    const Render::Color queuedColor = text ? color : ResolveLinuxUIFallbackColor(color, text, diffuseTexture);
    quads.push_back(LinuxQueuedUIQuad(quad, queuedColor, diffuseTexture, text));
    ++g_linuxOpenGLUiRendererStats.queued2DQuads;
    if (text)
      ++g_linuxOpenGLUiRendererStats.queued2DTextQuads;
    if (diffuseTexture)
      ++g_linuxOpenGLUiRendererStats.queuedTextured2DQuads;
    queueRendered = false;
#else
    (void)quad;
    (void)color;
    (void)text;
    (void)renderMaterial;
#endif
  }

  bool initialized;
  Strong<FlashRenderer> flashRenderer;
  CVec4 resolutionCoefs;
  bool forbidSaturation;
  bool textStarted;
  unsigned int textFirstQuad;
  bool queueRendered;
  bool queueOverflowed;
  int currentFlashPart;
  vector<UIRect> cropRects;
  vector<LinuxQueuedUIQuad> quads;
  vector<LinuxQueuedFlashPart> flashParts;
  vector<LinuxQueuedRenderPart> renderParts;
};

} // namespace

IUIRenderer* GetUIRenderer()
{
  static NullUIRenderer uiRenderer;
  return &uiRenderer;
}

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
const LinuxOpenGLUiRendererStats& GetLinuxOpenGLUiRendererStats()
{
  return g_linuxOpenGLUiRendererStats;
}

void ResetLinuxOpenGLUiRendererStats()
{
  g_linuxOpenGLUiRendererStats = LinuxOpenGLUiRendererStats();
}

void AddLinuxOpenGLUiRendererFlashStats(unsigned int parts, unsigned int commands, unsigned int scissorCommands, unsigned int maskCommands, unsigned int blendCommands, unsigned int lineCommands, unsigned int lineVertices, unsigned int texturedCommands, unsigned int repeatCommands, unsigned int clampCommands)
{
  g_linuxOpenGLUiRendererStats.renderedFlashParts += parts;
  g_linuxOpenGLUiRendererStats.renderedFlashCommands += commands;
  g_linuxOpenGLUiRendererStats.renderedFlashScissorCommands += scissorCommands;
  g_linuxOpenGLUiRendererStats.renderedFlashMaskCommands += maskCommands;
  g_linuxOpenGLUiRendererStats.renderedFlashBlendCommands += blendCommands;
  g_linuxOpenGLUiRendererStats.renderedFlashLineCommands += lineCommands;
  g_linuxOpenGLUiRendererStats.renderedFlashLineVertices += lineVertices;
  g_linuxOpenGLUiRendererStats.renderedFlashTexturedCommands += texturedCommands;
  g_linuxOpenGLUiRendererStats.renderedFlashRepeatCommands += repeatCommands;
  g_linuxOpenGLUiRendererStats.renderedFlashClampCommands += clampCommands;
}

void SetLinuxOpenGLUiRendererFlashTextStyle(int partID, const Texture2DRef& texture, bool drawBevel, const Color& bevelColor)
{
  NullUIRenderer* uiRenderer = dynamic_cast<NullUIRenderer*>(GetUIRenderer());
  if (uiRenderer)
    uiRenderer->SetFlashTextStyle(partID, texture, drawBevel, bevelColor);
}
#endif

UIRenderMaterial::UIRenderMaterial()
  : renderMaterial(0)
  , texturePoolId(0)
{
}

UIRenderMaterial::UIRenderMaterial(const UIRenderMaterial& other)
  : renderMaterial(0)
  , texturePoolId(0)
{
  SetDBMaterial(other.dbMaterial, other.texturePoolId);
}

UIRenderMaterial& UIRenderMaterial::operator=(const UIRenderMaterial& other)
{
  SetDBMaterial(other.dbMaterial, other.texturePoolId);
  return *this;
}

UIRenderMaterial::UIRenderMaterial(const NDb::BaseUIMaterial* material)
  : renderMaterial(0)
  , texturePoolId(0)
{
  SetDBMaterial(material, 0);
}

UIRenderMaterial::UIRenderMaterial(const NDb::BaseUIMaterial* material, void* texturePool)
  : renderMaterial(0)
  , texturePoolId(0)
{
  SetDBMaterial(material, texturePool);
}

UIRenderMaterial::~UIRenderMaterial()
{
  if (renderMaterial)
    delete renderMaterial;
}

void UIRenderMaterial::Release()
{
  if (renderMaterial)
    delete renderMaterial;

  renderMaterial = 0;
  dbMaterial = 0;
  texturePoolId = 0;
}

void UIRenderMaterial::CreateDefaultMaterial()
{
  if (renderMaterial)
    delete renderMaterial;

  renderMaterial = new LinuxUIRenderMaterial();
  texturePoolId = 0;
}

void UIRenderMaterial::SetDBMaterial(const NDb::BaseUIMaterial* material, void* texturePool, bool forceReload)
{
  if ((material == dbMaterial) && !forceReload && renderMaterial)
    return;

  dbMaterial = material;
  texturePoolId = texturePool;

  if (renderMaterial)
    delete renderMaterial;

  if (!material)
  {
    renderMaterial = 0;
    return;
  }

  LinuxUIRenderMaterial* linuxMaterial = new LinuxUIRenderMaterial();
  linuxMaterial->SetDBMaterial(material, texturePool);
  renderMaterial = linuxMaterial;
}

Render::BaseMaterial* UIRenderMaterial::GetRenderMaterial()
{
  return renderMaterial;
}

const NDb::BaseUIMaterial* UIRenderMaterial::GetDBMaterial() const
{
  return dbMaterial;
}

} // namespace Render

#else

#include "uirenderer.hpp"

#include "FlashRendererInterface.h"
#include "TextureManager.h"
#include "smartrenderer.h"
#include "shadercompiler.h"
#include "../System/Float16.h"
#include "MaterialSpec.h"
#include "batch.h"
#include "GlobalMasks.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
namespace Render
{

DECLARE_NULL_RENDER_FLAG

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static const UIRect noCrop(-1,-1,-1,-1);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static NDebug::DebugVar<int> render_QuadCounter( "UIQuads", "Render" );

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CAlignedFreeHelper
{
	void *p;
public:
	CAlignedFreeHelper( void *_p ): p( _p ) {}
	~CAlignedFreeHelper() { if (p) Aligned_Free(p); p = 0; }
	void Cancel() { p = 0; }
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
IUIRenderer* GetUIRenderer()
{
  static DeviceLostWrapper<UIRenderer> uiRenderer;
  return &uiRenderer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
UIRenderer::UIRenderer() :
  initialized(false),
  queeOverflowed(false),
  resolutionCoefs( 1.0f, 1.0f, 0.0f, 0.0f ),
  projMatrix( CVec3(0, 0, 0) ),
  viewMatrix( CVec3(0, 0, 0) ), 
  invViewMatrix( CVec3(0, 0, 0) ),
  projViewMatrix( CVec3(0, 0, 0) ), 
  invProjViewMatrix( CVec3(0, 0, 0) ),
  mode(E3DMode::_2D),
  current3DBlock(NULL),

  saturationValue(0.f),
  saturationColor(1.f,1.f,1.f,1.f),
  saturate(false),
  forbidSaturate(false),

  que2D( ques[0] ),
  que3D( ques[1] )
{

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
UIRenderer::~UIRenderer()
{

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool UIRenderer::Initialize()
{
	if( initialized )
		return false;

  flashRenderer = new FlashRenderer();
  textureCache = new UITextureCache();

  // Check if compiler made any padding
  NI_STATIC_ASSERT( sizeof(VertexStride) == 28, VertexStride_size_wrong );
	NI_STATIC_ASSERT( sizeof(QuadStride) == 28*4, QuadStride_size_wrong );

		// Initialize vertex format
	{
		VertexFormatDescriptor formatDescriptor;
		formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 0, VERTEXELEMENTTYPE_FLOAT2, VERETEXELEMENTUSAGE_POSITION, 0) );
		formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 8, VERTEXELEMENTTYPE_FLOAT2, VERETEXELEMENTUSAGE_TEXCOORD, 0) );
		formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 16, VERTEXELEMENTTYPE_FLOAT2,VERETEXELEMENTUSAGE_TEXCOORD, 1) );
		formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 24, VERTEXELEMENTTYPE_D3DCOLOR, VERETEXELEMENTUSAGE_COLOR, 0) );

		pVDecl = SmartRenderer::GetVertexFormatDeclaration( formatDescriptor );
		dipDesc.primitiveType = RENDERPRIMITIVE_TRIANGLELIST;
	}

		// Build indexes buffer
	{
		const int quadsIndexesCount = QUAD_INDEX_COUNT * QUAD_MAX_COUNT;
		unsigned int *pIndexes = (unsigned int *)Aligned_MAlloc( quadsIndexesCount * sizeof(unsigned int), 16 );
		if ( !pIndexes )
		{
			NI_ASSERT( 0, "UIRenderer: Failed to allocate memory (temporary indexes)!" );
			return false;
		}

		CAlignedFreeHelper freeIndexes( pIndexes );

		int quadVertex = 0;
		unsigned int *pIndex = pIndexes;
		for( unsigned int i = 0; i < quadsIndexesCount; i+=6 )
		{
			*pIndex++ = quadVertex;
			*pIndex++ = quadVertex + 1;
			*pIndex++ = quadVertex + 2;
			*pIndex++ = quadVertex;
			*pIndex++ = quadVertex + 2;
			*pIndex++ = quadVertex + 3;
			quadVertex += 4;
		}

		const int indexesSize = quadsIndexesCount * sizeof(unsigned int);
		pIB = CreateIB( indexesSize, RENDER_POOL_MANAGED, pIndexes );
		if ( !Get(pIB) )
		{
			NI_ASSERT( 0, "UIRenderer: Failed to create index buffer!" );
			return false;
		}
	}

  const int quadsVerticesSize = QUAD_MAX_COUNT * sizeof(QuadStride);

	// Create vertex buffer
	pVB.Resize(quadsVerticesSize);
	if ( !Get(pVB) )
	{
		NI_ASSERT( 0, "UIRenderer: Failed to create vertex buffer!" );
		return false;
	}

	// Other initializations
	SetResolutionCoefs( 1.0f / 1024.0f , 1.0f / 768.0f, 1024.f / 1280.f, 768.0f / 1024.f );

  //Reset transforms
  SHMatrix null(CVec3(0,0,0));
  projMatrix = null;
  viewMatrix = null;
  invViewMatrix = null;
  projViewMatrix = null;
  invProjViewMatrix = null;

  // Flash
  if ( !flashRenderer->Initialize() )
    return false;

  // Done
	initialized = true;
	StartFrame();
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::Release()
{
	if ( !initialized )
		return;

  flashRenderer->Release();
  flashRenderer = 0;

  textureCache->Release();
  textureCache = 0;

  pVB.Reset();
	pIB = 0;
	pVDecl = 0;

	initialized = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
inline UIRenderer::RenderQueue& UIRenderer::GetQueue()
{
  return ( E3DMode::_2D == mode ) ? que2D : que3D;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::StartFrame()
{
  if(RENDER_DISABLED)
    return;

  flashRenderer->StartFrame();

  render_QuadCounter.SetValue(que2D.quadCounter + que3D.quadCounter);
  for(int i = 0; i < 2; ++i)
  {
    ques[i].quadCounter = 0;
	  ques[i].quadFirstText = quadFirstTextOff;
    ques[i].parts.clear();
  }

  mode = E3DMode::_2D;
  current3DBlock = NULL;
  blocksSorter.clear();

	cropRects.clear();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::PushCrop( const UIRect & cropRect )
{
	cropRects.push_back( cropRect );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::PushNoCrop()
{
	cropRects.push_back( noCrop );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::PopCrop()
{
	NI_VERIFY( !cropRects.empty(), "UIRenderer: Crop rects stack underflow!", return );
	cropRects.pop_back();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool UIRenderer::CropQuadInternal( UIQuad& quad, const UIRect & cropRect )
{
	CTRect<float> croppedRect( quad.tl.x, quad.tl.y,  quad.br.x, quad.br.y );
  CTRect<float> floatCropRect( cropRect );
	croppedRect.Intersect( floatCropRect );

	if ( croppedRect.minx >= croppedRect.maxx )
		return false;
	if ( croppedRect.miny >= croppedRect.maxy )
		return false;

	if ( quad.tl.x < croppedRect.x1 )
	{
		float dx = ( quad.br.x - quad.tl.x );
		float cx = ( croppedRect.x1 - quad.tl.x );
		quad.uv.u += (( quad.uvl.u - quad.uv.u ) * cx) / dx;
		quad.uv2.u += (( quad.uvl2.u - quad.uv2.u ) * cx) / dx;
		quad.tl.x = croppedRect.x1;
	}

	if ( quad.tl.y < croppedRect.y1 )
	{
		float dy = ( quad.br.y - quad.tl.y );
		float cy = ( croppedRect.y1 - quad.tl.y );
		quad.uv.v += (( quad.uvl.v - quad.uv.v ) * cy) / dy;
		quad.uv2.v += (( quad.uvl2.v - quad.uv2.v ) * cy) / dy;
		quad.tl.y = croppedRect.y1;
	}

	if ( quad.br.x > croppedRect.x2 )
	{
		float dx = ( quad.br.x - quad.tl.x );
		float cx = ( croppedRect.x2 - quad.br.x );
		quad.uvl.u += (( quad.uvl.u - quad.uv.u ) * cx) / dx;
		quad.uvl2.u += (( quad.uvl2.u - quad.uv2.u ) * cx) / dx;
		quad.br.x = croppedRect.x2;
	}

	if ( quad.br.y > croppedRect.y2 )
	{
		float dy = ( quad.br.y - quad.tl.y );
		float cy = ( croppedRect.y2 - quad.br.y );
		quad.uvl.v += (( quad.uvl.v - quad.uv.v ) * cy) / dy;
		quad.uvl2.v += (( quad.uvl2.v - quad.uv2.v ) * cy) / dy;
		quad.br.y = croppedRect.y2;
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void ExtPointTransform( float & x, float & y, const CVec2 & pivot, float ksn, float kcs )
{
	float px = x - pivot.x;
	float py = y - pivot.y;
	float nx = pivot.x + px * kcs + py * ksn;
	float ny = pivot.y - px * ksn + py * kcs;
	x = nx;
	y = ny;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
unsigned int UIRenderer::AddQuadInternal( const UIQuad & _quad, const Color& _color )
{
  RenderQueue & queue = GetQueue();
	NI_VERIFY( initialized, "UIRenderer: Not initialized!", return 0 );

  if(queue.quadCounter < QUAD_MAX_COUNT)
    queeOverflowed = false;
  else {
    NI_VERIFY_TRACE( queeOverflowed, "UIRenderer: Quads overflow!", queeOverflowed = true );
    return 0;
  }

  QuadStride & stride = queue.quadsQueue[queue.quadCounter];

	//	1-----0
	//	|		/	|
	//	|	 /  |
	//	|	/		|
	//	2-----3

//   const Render::RenderMode & renderMode = Render::GetRenderer()->GetCurrentRenderMode();
//   const float kw = renderMode.width > 0 ? ( float(1280) / float(renderMode.width) ) : 1.0f;
//   const float kh = renderMode.height > 0 ? ( float(1024) / float(renderMode.height) ) : 1.0f;

  const float shiftX = 0;//0.5f * kw; 
  const float shiftY = 0;//0.5f * kh; 

	////0
	stride.v0.x = _quad.br.x - shiftX;
	stride.v0.y = _quad.tl.y - shiftY;
	stride.v0.u = _quad.uvl.x;
	stride.v0.v = _quad.uv.y;
	stride.v0.u2 = _quad.uvl2.x;
	stride.v0.v2 = _quad.uv2.y;
	stride.v0.color.Dummy = _color.Dummy;
	////1
	stride.v1.x = _quad.tl.x - shiftX;
	stride.v1.y = _quad.tl.y - shiftY;
	stride.v1.u = _quad.uv.x;
	stride.v1.v = _quad.uv.y;
	stride.v1.u2 = _quad.uv2.x;
	stride.v1.v2 = _quad.uv2.y;
	stride.v1.color.Dummy = _color.Dummy;
	////2
	stride.v2.x = _quad.tl.x - shiftX;
	stride.v2.y = _quad.br.y - shiftY;
	stride.v2.u = _quad.uv.x;
	stride.v2.v = _quad.uvl.y;
	stride.v2.u2 = _quad.uv2.x;
	stride.v2.v2 = _quad.uvl2.y;
	stride.v2.color.Dummy = _color.Dummy;
	////3
	stride.v3.x = _quad.br.x - shiftX;
	stride.v3.y = _quad.br.y - shiftY;
	stride.v3.u = _quad.uvl.x;
	stride.v3.v = _quad.uvl.y;
	stride.v3.u2 = _quad.uvl2.x;
	stride.v3.v2 = _quad.uvl2.y;
	stride.v3.color.Dummy = _color.Dummy;

	if ( _quad.ext )
	{
		CVec2 pivot = _quad.pivot;
		pivot.x -=  shiftX;
		pivot.y -=  shiftY;
		float ksn = sinf( _quad.angle ) * _quad.scale;
		float kcs = cosf( _quad.angle ) * _quad.scale;
		ExtPointTransform( stride.v0.x, stride.v0.y, pivot, ksn, kcs );
		ExtPointTransform( stride.v1.x, stride.v1.y, pivot, ksn, kcs );
		ExtPointTransform( stride.v2.x, stride.v2.y, _quad.pivot, ksn, kcs );
		ExtPointTransform( stride.v3.x, stride.v3.y, _quad.pivot, ksn, kcs );

		// Following code is test for rotating/scaling quads:
		//SVector s( _quad.tl.x + (_quad.br.x - _quad.tl.x) * 0.66f, _quad.tl.y + (_quad.br.y - _quad.tl.y) * 0.3f );
		//_quad.SetExtParams( s, (GetTickCount() * 3.14f)/1000.0f, sinf(GetTickCount()*3.14f*0.001f)*2.0f+1.0f );
	}

	return ++queue.quadCounter;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::BeginFlashParts( int startFlashElement )
{
  RenderQueue & queue = GetQueue();

  queue.currentFlashPart = queue.parts.size();
  queue.parts.push_back( SRenderPart( startFlashElement, -1 ) );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::EndFlashParts( int _lastFlashElement )
{
  RenderQueue & queue = GetQueue();

  NI_VERIFY( queue.currentFlashPart >= 0, "Flash render parts was not started", return );

  SRenderPart & part = queue.parts[queue.currentFlashPart];

  NI_VERIFY( part.firstFlashElement >= 0, "", return );
  NI_VERIFY( _lastFlashElement >= part.firstFlashElement, "", return );

  part.lastFlashElement = _lastFlashElement;
  queue.currentFlashPart = -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::AddQuad( UIQuad & _quad, Render::BaseMaterial* _renderMaterial, const SMaterialParams & params )
{
  if ( !_renderMaterial )
    return;

  if ( !_quad.ext && !cropRects.empty() && !cropRects.back().IsSame( noCrop ) )
    if ( !CropQuadInternal( _quad, cropRects.back() ) )
      return;

  RenderQueue & queue = GetQueue();

  CVec4 color1, color2, coefs;

  _renderMaterial->ModifyCoefs( params.coef3, params.coef4 );
  coefs.z = params.coef3;
  coefs.w = params.coef4;

  const unsigned int next = AddQuadInternal( _quad, SaturateColor( params.color0 ) );
  bool startNewPart = false;

  if (next == 1 || ( mode == E3DMode::_3DNewBlock ))
  {
    startNewPart = true;
  }
  else
  {
    const SRenderPart & prev = queue.parts.back();

    if ( prev.renderMaterial != _renderMaterial )
      startNewPart = true;
  }

  if ( startNewPart )
  {
    queue.parts.push_back( SRenderPart( 
      _renderMaterial,
      next - 1, 1,
      color1, color2,
      coefs ) );

    if ( mode == E3DMode::_3DNewBlock )
      mode = E3DMode::_3D;
  }
  else
  {
    queue.parts.back().quadCount += 1; 
    NI_ASSERT( queue.parts.back().quadCount < MAX_DIP_LENGTH, "UIRenderer: Index buffer overflow #2!" );
  }

  queue.quadFirstText = quadFirstTextOff;

  flashRenderer->BreakQueue();  
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::BeginText()
{
	GetQueue().quadFirstText = GetQueue().quadCounter;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::AddTextQuad( UIQuad & _quad, const SMaterialParams & params )
{
  RenderQueue & queue = GetQueue();
	NI_VERIFY( queue.quadFirstText != quadFirstTextOff, "UIRenderer: Trying to render text outside BeginText/EndText!", return );

	_quad.ext = false; // not supported for texts

	if ( !cropRects.empty() && !cropRects.back().IsSame( noCrop ) )
		if ( !CropQuadInternal( _quad, cropRects.back() ) )
			return;

  if ( queue.currentFlashPart >= 0 ) // in Flash
    AddQuadInternal( _quad, params.color0 );
  else
    AddQuadInternal( _quad, SaturateColor( params.color0 ) );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::EndText( Render::BaseMaterial* _renderMaterial )
{
  if ( !_renderMaterial )
    return;

  RenderQueue & queue = GetQueue();
	NI_VERIFY( queue.quadFirstText != quadFirstTextOff, "UIRenderer: Trying to render text outside BeginText/EndText!", return );
	const unsigned int quadLastText = queue.quadCounter;
	
	NI_VERIFY( queue.quadFirstText <= quadLastText, "UIRenderer: Text rendering failure!", return );

	const unsigned int len = quadLastText - queue.quadFirstText;
	if ( len == 0 )
		return;

	NI_ASSERT( len < MAX_DIP_LENGTH, "UIRenderer: Index buffer overflow #3!" );

 	CVec4 color1, color2, coefs;

  queue.parts.push_back( SRenderPart(
    _renderMaterial,
    queue.quadFirstText, len,
    color1, color2,
    coefs ) );

  if ( queue.currentFlashPart >= 0 )
  {
    flashRenderer->RenderText( queue.parts.size() - 1 );  
    queue.parts.back().textInFlash = true;
  }

  if ( mode == E3DMode::_3DNewBlock )
    mode = E3DMode::_3D;
		
	queue.quadFirstText = quadFirstTextOff;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::SetViewMatrices(const SHMatrix & view, const SHMatrix & projection)
{
  viewMatrix = view;
  projMatrix = projection;
  Invert(&invViewMatrix, viewMatrix);

  Multiply(&projViewMatrix, projection, view);
  Invert(&invProjViewMatrix, projViewMatrix);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::SetWorldMatrix(const SHMatrix & world, ETransformMode::Enum transformMode, const CVec3 & pivot, float depthBias)
{
  SHMatrix combined;
  Multiply(&combined, projViewMatrix, world);
 
  mode = E3DMode::_3DNewBlock;

  int firstPart = que3D.parts.size();

  float depth = CalcDepth(pivot);
  pair<BlocksSorter::iterator, bool> result =
    blocksSorter.insert(BlocksSorter::value_type(SBlockKey(-depth, blocksSorter.size()), Parts3DBlock(firstPart, 0, combined)));
  NI_ASSERT(result.second, "Parts 3D block was not inserted into map");

  current3DBlock = &result.first->second;

  if ( ETransformMode::DepthOnly == transformMode )
  {
    CVec4 t;
    combined.RotateHVector(&t, CVec4(0, 0, 0, 1));
    current3DBlock->forcedZ = t.z + depthBias;
    current3DBlock->forcedW = t.w;
    current3DBlock->transform.Set(CVec3(0, 0, 0));
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::ResetWorldMatrix()
{
  if ( current3DBlock && mode == E3DMode::_3D || mode == E3DMode::_3DNewBlock )
  {
    current3DBlock->size = que3D.parts.size() - current3DBlock->firstPart;
    current3DBlock = NULL;
  }

  mode = E3DMode::_2D;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::GetBillboardMatrix(SHMatrix * pCombined, const SHMatrix & world, ETransformMode::Enum transformMode, const CVec3 & pivot, float depthBias)
{
  //FIXME: Не очень оптимальная работа с матицами
  //TODO: iA: Надо бы упростить расчет матриц; Думаю, можно свести все к 3-4 умножениям в самом сложном случае

  //Rotation = PivotOffset * (~viewMatrix) * swap * (~PivotOffset);
  SHMatrix RotLeft;
  SHMatrix tmp = viewMatrix;
  for(int i = 0; i < 3; ++i) tmp.m[i][3] = 0;
  Invert(&RotLeft, tmp);

  for(int i = 0; i < 3; ++i) RotLeft.m[i][3] = pivot[i];

  SHMatrix RotRight(
    1, 0, 0, -pivot.x,
    0, 0, 1, -pivot.z,
    0, 1, 0, -pivot.y + depthBias,
    0, 0, 0, 1
    );

  SHMatrix Rotation;
  Multiply(&Rotation, RotLeft, RotRight);

/*
  Render::Color C[3] = {Color(0xffff0000), Color(0xff00ff00), Color(0xff0000ff)};
  for(int i = 0; i < 3; ++i)
  {
    CVec3 X(0,0,0);
    X[i] = 5;
    CVec3 A, B;
    Rotation.RotateHVector(&A, pivot);
    Rotation.RotateHVector(&B, pivot + X * 2);
    Render::DebugRenderer::DrawLine3D(A, B, C[i], C[i], false);
    Render::DebugRenderer::DrawLine3D(pivot, pivot + X, C[i], C[i], false);
  }
*/
  if ( transformMode == ETransformMode::AxisBillboard )
  {
    const CVec3 axis(0, 0, 1);
    CVec3 tAxis;
    Rotation.RotateVector(&tAxis, axis);
    Normalize(&tAxis);
    CVec3 XP = axis ^ tAxis;
    float XPLen = XP.Length();
    float angle = asinf(XPLen); //FIXME: we just ignore 180 grad case
    if(fabs(angle) > 1e-4f)
    {
      XP /= XPLen;
      CQuat q(-angle, XP, false);
      SHMatrix axisFix(q);

      Rotation = SHMatrix(pivot) * axisFix * SHMatrix(-pivot) * Rotation;
      //Multiply(&tmp, axisFix, RotLeft);
      //RotLeft = tmp;
    }
  }

  //*pCombined = Rotation * world
  Multiply(pCombined, Rotation, world);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::GetRay(CVec3 * pOrigin, CVec3 * pDir, int sx, int sy)
{
  invViewMatrix.RotateHVector(pOrigin, CVec3(0, 0, 0));

  CVec4 point, screen(sx * resolutionCoefs.x - 1.0f, 1.0f - sy * resolutionCoefs.y, 1.0f, 1.0f);
  invProjViewMatrix.RotateHVector(&point, screen);

  for(int i = 0; i < 3; ++i)
    (*pDir)[i] = point[i] / point.w - (*pOrigin)[i];
  Normalize(pDir);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float UIRenderer::CalcDepth(const CVec3 & point)
{
  CVec3 localPos;
  viewMatrix.RotateHVector(&localPos, point);
  return localPos.z;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::SetResolutionCoefs( const float x, const float y, const float widthScale, const float heightScale )
{
	resolutionCoefs.x = x;
	resolutionCoefs.y = y;
  resolutionCoefs.z = widthScale;
  resolutionCoefs.w = heightScale;

  flashRenderer->SetResolutionCoefs( x, y, widthScale, heightScale );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const CVec4& UIRenderer::GetResolutionCoefs() const
{
  return resolutionCoefs;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::SetFontTextureSize( const int width, const int height )
{
  fontTextureSize.z = width;
  fontTextureSize.w = height;
  fontTextureSize.x = fontTextureSize.z > 0 ? 1.0f / fontTextureSize.z : 0.0f;
  fontTextureSize.y = fontTextureSize.w > 0 ? 1.0f / fontTextureSize.w : 0.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::RenderPart( const SRenderPart & part, bool alphaTest )
{
  if ( part.renderMaterial )
    part.renderMaterial->PrepareRenderer();

  if ( alphaTest )
    GetDevice()->SetRenderState( D3DRS_ALPHATESTENABLE, TRUE );

  dipDesc.baseVertexIndex = part.quadFirst * QUAD_VERTEX_COUNT;	
  dipDesc.numVertices = part.quadCount * QUAD_VERTEX_COUNT;
  dipDesc.primitiveCount = part.quadCount * QUAD_PRIMITIVE_COUNT;

  SmartRenderer::DrawIndexedPrimitive( dipDesc );

  if ( alphaTest )
    GetDevice()->SetRenderState( D3DRS_ALPHATESTENABLE, FALSE );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::BeginQueue()
{
  if(RENDER_DISABLED)
    return;

  StartFrame();
  flashRenderer->BeginQueue();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::EndQueue()
{
  if(RENDER_DISABLED)
    return;
  flashRenderer->EndQueue();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::Render( ERenderWhat::Enum what, const Render::Texture2DRef& pMainRT0, const Render::Texture2DRef& pMainRT0Copy )
{
	if ( !initialized )
		return;

  RenderQueue & queue = (what == ERenderWhat::_2D) ? que2D : que3D;

	if( queue.quadCounter == 0 )
		return;

	if ( ERenderWhat::_2D == what )
  {
	  GetStatesManager()->SetStateDirect( D3DRS_ZENABLE, D3DZB_FALSE );
  }
  else if ( ERenderWhat::_3D == what )
  {
    GetStatesManager()->SetStateDirect( D3DRS_ZENABLE, D3DZB_TRUE );
    GetStatesManager()->SetStateDirect( D3DRS_ZWRITEENABLE, FALSE );
  }
  else
  {
    NI_ALWAYS_ASSERT("Dont know what to render");
  }

  GetStatesManager()->SetStateDirect( D3DRS_CULLMODE, D3DCULL_NONE );

	FillVB( Get(pVB), queue.quadCounter * sizeof(QuadStride), &queue.quadsQueue, D3DLOCK_DISCARD );

	GetRenderer()->SetPixelShaderConstantsVector4( PSHADER_LOCALCONST3, resolutionCoefs );
	GetRenderer()->SetVertexShaderConstantsVector4( VSHADER_LOCALCONST3, resolutionCoefs );

  // Font Texture size
  GetRenderer()->SetPixelShaderConstantsVector4( PSHADER_LOCALCONST2, fontTextureSize );
  GetRenderer()->SetVertexShaderConstantsVector4( VSHADER_LOCALCONST2, fontTextureSize );

  bool wasFlash = true; // to bind buffers at first part

  if ( ERenderWhat::_2D == what )
  {
	  for( int i = 0; i < queue.parts.size(); ++i )
    {
      SRenderPart& renderPart = queue.parts[i];

      if ( renderPart.textInFlash )
        continue;

      if ( renderPart.flashElement )
      {
        NI_ASSERT( renderPart.lastFlashElement >= 0, "Not finished flash render part!" );
        flashRenderer->Render( renderPart.firstFlashElement, renderPart.lastFlashElement, pMainRT0, pMainRT0Copy );
        wasFlash = true;
      }
      else
      {
        if ( wasFlash )
        {
          PrepareRender();
        }

        RenderPart( queue.parts[i], false );

        wasFlash = false;
      }
    }
  }
  else //what == eRender3D
  {
    PrepareRender();

    for ( BlocksSorter::iterator it = blocksSorter.begin(); it != blocksSorter.end(); ++it )
    {
      const Parts3DBlock & block = it->second;

      GetRenderer()->SetVertexShaderConstantsMatrix( WORLD, block.transform );
      GetRenderer()->SetVertexShaderConstantsFloat( VSHADER_LOCALCONST5, block.forcedZ );
      GetRenderer()->SetVertexShaderConstantsFloat( VSHADER_LOCALCONST6, block.forcedW );

      for ( int i = 0; i < block.size; ++i )
      {
        int partIdx = block.firstPart + i;
        NI_ASSERT(partIdx >= 0 && partIdx < queue.parts.size(), "Part index out of range");
        RenderPart( queue.parts[partIdx], false );
      }
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::RenderPart( int _partID, ERenderWhat::Enum what, bool alphaTest )
{
  RenderQueue & queue = (what == ERenderWhat::_2D) ? que2D : que3D;
  NI_ASSERT( _partID >= 0 && _partID < queue.parts.size(), "Part index out of range" );
  RenderPart( queue.parts[_partID], alphaTest );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
BaseMaterial* UIRenderer::GetPartMaterial( int _partID, ERenderWhat::Enum what )
{
  RenderQueue & queue = (what == ERenderWhat::_2D) ? que2D : que3D;
  NI_VERIFY( _partID >= 0 && _partID < queue.parts.size(), "Part index out of range", return 0 );
  return queue.parts[_partID].renderMaterial;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::PrepareRender()
{
  const Render::RenderMode & renderMode = Render::GetRenderer()->GetCurrentRenderMode();

  SmartRenderer::BindVertexDeclaration( pVDecl );
  SmartRenderer::BindIndexBuffer( pIB );
  SmartRenderer::BindVertexBuffer( 0, Get(pVB), sizeof(VertexStride)	);

  //Clean combined transform matrix
  GetRenderer()->SetVertexShaderConstantsMatrix( WORLD, SHMatrix( CVec3( -1.f / renderMode.width, +1.f / renderMode.height, 0 )) );
  GetRenderer()->SetVertexShaderConstantsFloat( VSHADER_LOCALCONST5, 0.0f );
  GetRenderer()->SetVertexShaderConstantsFloat( VSHADER_LOCALCONST6, 1.0f );
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::PrepareRenderFromFlash()
{
  SmartRenderer::BindVertexDeclaration( pVDecl );
  SmartRenderer::BindIndexBuffer( pIB );
  SmartRenderer::BindVertexBuffer( 0, Get(pVB), sizeof(VertexStride)	);
  
  GetRenderer()->SetVertexShaderConstantsFloat( VSHADER_LOCALCONST5, 0.0f );
  GetRenderer()->SetVertexShaderConstantsFloat( VSHADER_LOCALCONST6, 1.0f );
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderer::SetSaturation( float _val, const CVec4& _color, bool _saturate )
{
  saturationValue = _val;
  saturationColor = _color; 
  saturate = _saturate;
}

bool UIRenderer::ForbidSaturation( bool _forbid)
{
  bool oldValue = forbidSaturate;
  forbidSaturate = _forbid;
  return oldValue;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Render::Color UIRenderer::SaturateColor( const Render::Color& _color )
{
  if ( !saturate || forbidSaturate )
    return _color;

  return Render::Color( Render::Saturate( Render::HDRColor( _color ), saturationValue, saturationColor ) );  
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
UIRenderMaterial::UIRenderMaterial() :
  renderMaterial( 0 ),
  texturePoolId( 0 )
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
UIRenderMaterial::UIRenderMaterial( const UIRenderMaterial& other ) :
  renderMaterial( 0 ),
  texturePoolId( 0 )
{
  SetDBMaterial( other.dbMaterial, other.texturePoolId );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
UIRenderMaterial& UIRenderMaterial::operator = ( const UIRenderMaterial& other )
{
  SetDBMaterial( other.dbMaterial, other.texturePoolId );

  return *this;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
UIRenderMaterial::UIRenderMaterial( const NDb::BaseUIMaterial* _material ) :
  renderMaterial( 0 ),
  texturePoolId( 0 )
{
  SetDBMaterial( _material, 0 );
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
UIRenderMaterial::UIRenderMaterial( const NDb::BaseUIMaterial* _material, void * _texturePoolId ) :
  renderMaterial( 0 )
{
  SetDBMaterial( _material, _texturePoolId );
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
UIRenderMaterial::~UIRenderMaterial()
{
  if ( renderMaterial )
    delete renderMaterial;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void  UIRenderMaterial::Release()
{
  if ( renderMaterial )
    delete renderMaterial;

  renderMaterial = 0;
  dbMaterial = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderMaterial::CreateDefaultMaterial()
{
  renderMaterial = static_cast<Render::BaseMaterial*>( Render::CreateRenderMaterial( NDb::UIBaseMaterial::typeId ) );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UIRenderMaterial::SetDBMaterial( const NDb::BaseUIMaterial * _material, void * _texturePoolId, bool forceReload /*= false*/ )
{
  if ( (_material == dbMaterial) && !forceReload && renderMaterial )
    return;

  dbMaterial = _material;

  if ( IsValid( dbMaterial ) )
  {
    if ( renderMaterial )
      delete renderMaterial;

    renderMaterial = static_cast<Render::BaseMaterial*>( Render::CreateRenderMaterialInPool( dbMaterial, _texturePoolId ) );
    texturePoolId = _texturePoolId;
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Render::BaseMaterial* UIRenderMaterial::GetRenderMaterial()
{
  return renderMaterial;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const NDb::BaseUIMaterial* UIRenderMaterial::GetDBMaterial() const
{
  return dbMaterial;
}


} // namespace Render

#endif
