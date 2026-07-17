#include "stdafx.h"

#if defined(PW_LINUX_NULL_RENDER)

#include "../UI/Flash/GameSWFIntegration/Image.h"
#include "FlashRenderer.h"
#include "TextureManager.h"
#include "uirenderer.h"

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
#include "../System/MainFrame.h"
#include <GL/gl.h>
#endif

namespace Render
{

namespace
{

flash::SWF_RGBA SampleLinuxGradient(const flash::SWF_GRADIENT& gradient, int ratio)
{
  if (gradient.GradientRecords.empty())
    return flash::SWF_RGBA(0, 0, 0, 0);

  if (ratio < gradient.GradientRecords[0].Ratio)
    return gradient.GradientRecords[0].Color;

  for (int i = 1; i < static_cast<int>(gradient.GradientRecords.size()); ++i)
  {
    if (gradient.GradientRecords[i].Ratio >= ratio)
    {
      const flash::SWF_GRADRECORD& gr0 = gradient.GradientRecords[i - 1];
      const flash::SWF_GRADRECORD& gr1 = gradient.GradientRecords[i];
      float f = 0.0f;
      if (gr0.Ratio != gr1.Ratio)
        f = (ratio - gr0.Ratio) / float(gr1.Ratio - gr0.Ratio);

      flash::SWF_RGBA result;
      result.Lerp(gr0.Color, gr1.Color, f);
      return result;
    }
  }

  return gradient.GradientRecords.back().Color;
}

void WriteLinuxGradientPixel(unsigned char* data, int pixelOffset, const flash::SWF_RGBA& color)
{
  data[pixelOffset + 0] = color.b;
  data[pixelOffset + 1] = color.g;
  data[pixelOffset + 2] = color.r;
  data[pixelOffset + 3] = color.a;
}

float SolveLinuxFlashGradientRadius(float a, float b, float c)
{
  const float determinant = b * b - 4.0f * a * c;
  if (determinant < 0.0f)
    return 0.0f;

  if (fabsf(a) < 0.00001f)
  {
    if (fabsf(b) < 0.00001f)
      return 0.0f;
    return -c / b;
  }

  const float root = sqrtf(determinant);
  const float x1 = (-b - root) / (2.0f * a);
  const float x2 = (-b + root) / (2.0f * a);
  return x1 < 0.0f ? x2 : x1;
}

Texture2DRef CreateLinuxLinearGradientTexture(const flash::SWF_GRADIENT& gradient)
{
  const int gradientSize = 256;
  Texture2DRef texture = Render::CreateTexture2D(gradientSize, 1, 1, RENDER_POOL_MANAGED, FORMAT_A8R8G8B8);
  if (!texture)
    return texture;

  LockedRect lockedRect = texture->LockRect(0, LOCK_DEFAULT);
  if (!lockedRect.data)
    return texture;

  for (int i = 0; i < gradientSize; ++i)
    WriteLinuxGradientPixel(lockedRect.data, i * 4, SampleLinuxGradient(gradient, i));

  texture->UnlockRect(0);
  return texture;
}

Texture2DRef CreateLinuxRadialGradientTexture(const flash::SWF_GRADIENT& gradient)
{
  const int gradientWidth = 64;
  const int gradientHeight = 64;
  Texture2DRef texture = Render::CreateTexture2D(gradientWidth, gradientHeight, 1, RENDER_POOL_MANAGED, FORMAT_A8R8G8B8);
  if (!texture)
    return texture;

  LockedRect lockedRect = texture->LockRect(0, LOCK_DEFAULT);
  if (!lockedRect.data)
    return texture;

  const float radiusY = (gradientHeight - 1) / 2.0f;
  const float radiusX = (gradientWidth - 1) / 2.0f;
  const float focalPoint = gradient.type == flash::EGradientType::Focal ? gradient.FocalPoint : 0.0f;

  for (int y = 0; y < gradientHeight; ++y)
  {
    for (int x = 0; x < gradientWidth; ++x)
    {
      const float nx = (x - radiusX) / radiusX;
      const float ny = (y - radiusY) / radiusY;
      const float ratioF = SolveLinuxFlashGradientRadius(
        focalPoint * focalPoint - 1.0f,
        2.0f * nx * focalPoint - 2.0f * focalPoint * focalPoint,
        nx * nx - 2.0f * nx * focalPoint + focalPoint * focalPoint + ny * ny);
      int ratio = static_cast<int>(floorf(255.5f * ratioF));
      ratio = Clamp(ratio, 0, 255);

      const int pixelOffset = (y * gradientWidth + x) * 4;
      WriteLinuxGradientPixel(lockedRect.data, pixelOffset, SampleLinuxGradient(gradient, ratio));
    }
  }

  texture->UnlockRect(0);
  return texture;
}

Texture2DRef CreateLinuxGradientTexture(const flash::SWF_GRADIENT& gradient)
{
  if (gradient.type == flash::EGradientType::Linear)
    return CreateLinuxLinearGradientTexture(gradient);

  return CreateLinuxRadialGradientTexture(gradient);
}

float ApplyLinuxFlashScale9GridCoord(float coord, const CVec4& consts)
{
  if (consts.x < coord && coord < consts.y)
    return consts.x + (coord - consts.x) * consts.z;
  if (consts.y <= coord)
    return coord + consts.w;

  return coord;
}

class LinuxBitmapInfo : public IBitmapInfo, public BaseObjectST
{
  NI_DECLARE_REFCOUNT_CLASS_2( LinuxBitmapInfo, IBitmapInfo, BaseObjectST );

public:
  LinuxBitmapInfo()
    : width(0)
    , height(0)
    , uv1(0.0f, 0.0f)
    , uv2(1.0f, 1.0f)
    , gradientTexture(false)
    , gradientType(flash::EGradientType::Linear)
  {
  }

  LinuxBitmapInfo( int bitmapWidth, int bitmapHeight )
    : width(bitmapWidth)
    , height(bitmapHeight)
    , uv1(0.0f, 0.0f)
    , uv2(1.0f, 1.0f)
    , gradientTexture(false)
    , gradientType(flash::EGradientType::Linear)
  {
    if (width > 0 && height > 0)
      texture = Render::CreateTexture2D(width, height, 1, RENDER_POOL_MANAGED, FORMAT_A8R8G8B8);
  }

  LinuxBitmapInfo( const Texture2DRef& sourceTexture, bool gradient = false, flash::EGradientType::Enum type = flash::EGradientType::Linear )
    : width(0)
    , height(0)
    , uv1(0.0f, 0.0f)
    , uv2(1.0f, 1.0f)
    , gradientTexture(gradient)
    , gradientType(type)
  {
    texture = sourceTexture;
    if (texture)
    {
      width = static_cast<int>(texture->GetWidth());
      height = static_cast<int>(texture->GetHeight());
    }
  }

  LinuxBitmapInfo( image::rgba* im, bool repeatable )
    : width(im ? im->m_width : 0)
    , height(im ? im->m_height : 0)
    , uv1(0.0f, 0.0f)
    , uv2(1.0f, 1.0f)
    , gradientTexture(false)
    , gradientType(flash::EGradientType::Linear)
  {
    (void)repeatable;
    if (!im || width <= 0 || height <= 0)
      return;

    texture = Render::CreateTexture2D(width, height, 1, RENDER_POOL_MANAGED, FORMAT_A8R8G8B8);
    if (!texture)
      return;

    LockedRect lockedRect = texture->LockRect(0, LOCK_DEFAULT);
    if (!lockedRect.data)
      return;

    for (int y = 0; y < height; ++y)
    {
      unsigned char* dst = lockedRect.data + y * lockedRect.pitch;
      const unsigned char* src = im->m_data + y * im->m_pitch;
      for (int x = 0; x < width; ++x)
      {
        dst[x * 4 + 0] = src[x * 4 + 2];
        dst[x * 4 + 1] = src[x * 4 + 1];
        dst[x * 4 + 2] = src[x * 4 + 0];
        dst[x * 4 + 3] = src[x * 4 + 3];
      }
    }

    texture->UnlockRect(0);
  }

  virtual int GetWidth() const { return width; }
  virtual int GetHeight() const { return height; }
  virtual const CVec2& GetUV1() const { return uv1; }
  virtual const CVec2& GetUV2() const { return uv2; }
  const Texture2DRef& GetTexture() const { return texture; }
  bool IsGradientTexture() const { return gradientTexture; }
  flash::EGradientType::Enum GetGradientType() const { return gradientType; }

  virtual IBitmapInfo* Clone()
  {
    LinuxBitmapInfo* clone = new LinuxBitmapInfo(width, height);
    clone->gradientTexture = gradientTexture;
    clone->gradientType = gradientType;
    if (!texture || !clone->texture)
      return clone;

    LockedRect dstRect = clone->texture->LockRect(0, LOCK_DEFAULT);
    LockedRect srcRect = texture->LockRect(0, LOCK_DEFAULT);
    if (dstRect.data && srcRect.data)
    {
      for (int y = 0; y < height; ++y)
        memcpy(dstRect.data + y * dstRect.pitch, srcRect.data + y * srcRect.pitch, width * 4);
    }
    texture->UnlockRect(0);
    clone->texture->UnlockRect(0);
    return clone;
  }

  virtual void Draw( IBitmapInfo* source, const flash::SWF_MATRIX& matrix, int x1, int y1, int x2, int y2 )
  {
    (void)matrix;
    if (!texture || x1 > x2 || y1 > y2)
      return;

    LinuxBitmapInfo* sourceBitmap = dynamic_cast<LinuxBitmapInfo*>(source);
    if (!sourceBitmap || !sourceBitmap->texture)
      return;

    x1 = Clamp(x1, 0, sourceBitmap->GetWidth() - 1);
    x2 = Clamp(x2, 0, sourceBitmap->GetWidth() - 1);
    y1 = Clamp(y1, 0, sourceBitmap->GetHeight() - 1);
    y2 = Clamp(y2, 0, sourceBitmap->GetHeight() - 1);

    LockedRect dstRect = texture->LockRect(0, LOCK_DEFAULT);
    LockedRect srcRect = sourceBitmap->texture->LockRect(0, LOCK_DEFAULT);
    if (dstRect.data && srcRect.data)
    {
      for (int y = y1; y <= y2 && y < height; ++y)
      {
        unsigned char* dst = dstRect.data + y * dstRect.pitch + x1 * 4;
        const unsigned char* src = srcRect.data + y * srcRect.pitch + x1 * 4;
        memcpy(dst, src, (x2 - x1 + 1) * 4);
      }
    }
    sourceBitmap->texture->UnlockRect(0);
    texture->UnlockRect(0);
  }

private:
  int width;
  int height;
  CVec2 uv1;
  CVec2 uv2;
  Texture2DRef texture;
  // Tracks textures synthesized from SWF gradient fills for native replay diagnostics.
  bool gradientTexture;
  flash::EGradientType::Enum gradientType;
};

LinuxBitmapInfo* GetLinuxBitmapInfo(IBitmapInfo* bitmapInfo)
{
  return dynamic_cast<LinuxBitmapInfo*>(bitmapInfo);
}

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
bool IsLinuxFlashMappedBlendMode(EFlashBlendMode::Enum blendMode)
{
  switch (blendMode)
  {
  case EFlashBlendMode::ADD:
  case EFlashBlendMode::MULTIPLY:
  case EFlashBlendMode::SCREEN:
  case EFlashBlendMode::DARKEN:
  case EFlashBlendMode::LIGHTEN:
  case EFlashBlendMode::SUBTRACT:
  case EFlashBlendMode::INVERT:
    return true;

  default:
    return false;
  }
}

void ApplyLinuxFlashBlendMode(EFlashBlendMode::Enum blendMode)
{
  glBlendEquation(GL_FUNC_ADD);

  switch (blendMode)
  {
  case EFlashBlendMode::ADD:
    glBlendFunc(GL_ONE, GL_ONE);
    break;

  case EFlashBlendMode::MULTIPLY:
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    break;

  case EFlashBlendMode::SCREEN:
    glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE);
    break;

  case EFlashBlendMode::DARKEN:
    glBlendFunc(GL_ONE, GL_ONE);
    glBlendEquation(GL_MIN);
    break;

  case EFlashBlendMode::LIGHTEN:
    glBlendFunc(GL_ONE, GL_ONE);
    glBlendEquation(GL_MAX);
    break;

  case EFlashBlendMode::SUBTRACT:
    glBlendFunc(GL_ONE, GL_ONE);
    glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
    break;

  case EFlashBlendMode::INVERT:
    glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
    break;

  default:
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    break;
  }
}

void BeginLinuxFlashSubmitMask(int& maskLevel)
{
  if (maskLevel == 0)
  {
    glEnable(GL_STENCIL_TEST);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glStencilMask(0xFFFFFFFFu);
  }

  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  glStencilFunc(GL_EQUAL, maskLevel, 0xFFFFFFFFu);
  glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
  ++maskLevel;
}

void EndLinuxFlashSubmitMask(int maskLevel)
{
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glStencilFunc(GL_EQUAL, maskLevel, 0xFFFFFFFFu);
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

void BeginLinuxFlashUnsubmitMask(int maskLevel)
{
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  glStencilFunc(GL_EQUAL, maskLevel, 0xFFFFFFFFu);
  glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
}

void DisableLinuxFlashMask(int& maskLevel)
{
  if (maskLevel > 0)
    --maskLevel;

  if (maskLevel > 0)
  {
    EndLinuxFlashSubmitMask(maskLevel);
  }
  else
  {
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilFunc(GL_ALWAYS, 0, 0xFFFFFFFFu);
  }
}
#endif

unsigned char ClampFlashColorChannel(float value)
{
  if (value <= 0.0f)
    return 0;
  if (value >= 255.0f)
    return 255;
  return static_cast<unsigned char>(value);
}

} // namespace

FlashRenderer::FlashRenderer()
  : currentBlendMode(EFlashBlendMode::NORMAL)
  , resolutionXCoef(1.0f)
  , resolutionYCoef(1.0f)
  , widthScale(1.0f)
  , heightScale(1.0f)
  , displayActive(false)
  , scale9GridActive(false)
  , scale9ConstX(0.0f, 0.0f, 1.0f, 0.0f)
  , scale9ConstY(0.0f, 0.0f, 1.0f, 0.0f)
  , scale9Trans(1.0f, 1.0f, 0.0f, 0.0f)
  , lineWidth(1.0f)
  , lineColor(255, 255, 255, 255)
  , nextTextWithBevel(false)
  , nextTextBevelColor(0, 0, 0, 255)
{
}

FlashRenderer::~FlashRenderer()
{
}

bool FlashRenderer::Initialize()
{
  drawCommands.reserve(256);
  return true;
}

void FlashRenderer::Release()
{
  drawCommands.clear();
  colorMatrixStack.clear();
  nextTextTexture = Texture2DRef();
  nextTextWithBevel = false;
  nextTextBevelColor = Color(0, 0, 0, 255);
  scale9GridActive = false;
}

void FlashRenderer::StartFrame()
{
  drawCommands.clear();
  colorMatrixStack.clear();
  nextTextTexture = Texture2DRef();
  nextTextWithBevel = false;
  nextTextBevelColor = Color(0, 0, 0, 255);
  ClearFillStyles();
  currentDisplayState = LinuxFlashDisplayState();
  displayActive = false;
  scale9GridActive = false;
}

void FlashRenderer::BeginQueue()
{
  drawCommands.clear();
  colorMatrixStack.clear();
  nextTextTexture = Texture2DRef();
  nextTextWithBevel = false;
  nextTextBevelColor = Color(0, 0, 0, 255);
  ClearFillStyles();
  currentDisplayState = LinuxFlashDisplayState();
  displayActive = false;
  scale9GridActive = false;
}

void FlashRenderer::EndQueue()
{
}

void FlashRenderer::BreakQueue()
{
}

void FlashRenderer::Render( int firstElement, int lastElement, const Render::Texture2DRef& pMainRT0, const Render::Texture2DRef& pMainRT0Copy )
{
  (void)pMainRT0;
  (void)pMainRT0Copy;
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
  if (drawCommands.empty() || !NMainFrame::MakeOpenGLContextCurrent())
    return;

  if (firstElement < 0)
    firstElement = 0;
  if (lastElement < firstElement)
    return;
  if (lastElement > static_cast<int>(drawCommands.size()))
    lastElement = static_cast<int>(drawCommands.size());

  GLint previousViewport[4] = { 0, 0, 0, 0 };
  glGetIntegerv(GL_VIEWPORT, previousViewport);

  glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_TRANSFORM_BIT | GL_TEXTURE_BIT | GL_VIEWPORT_BIT | GL_SCISSOR_BIT);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glDisable(GL_LIGHTING);
  glDisable(GL_STENCIL_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();

  unsigned int renderedCommands = 0;
  unsigned int renderedScissorCommands = 0;
  unsigned int renderedMaskCommands = 0;
  unsigned int renderedBlendCommands = 0;
  unsigned int renderedLineCommands = 0;
  unsigned int renderedLineVertices = 0;
  unsigned int renderedTexturedCommands = 0;
  unsigned int renderedRepeatCommands = 0;
  unsigned int renderedClampCommands = 0;
  unsigned int renderedScale9Commands = 0;
  unsigned int renderedScale9TexturedCommands = 0;
  unsigned int renderedGradientCommands = 0;
  unsigned int renderedFocalGradientCommands = 0;
  int maskLevel = 0;

  for (int i = firstElement; i < lastElement; ++i)
  {
    const LinuxFlashDrawCommand& command = drawCommands[i];
    if (command.kind == LinuxFlashDrawCommand::DrawGeometry && command.vertices.empty())
      continue;

    const LinuxFlashDisplayState& displayState = command.displayState;
    if (displayState.viewportWidth > 0 && displayState.viewportHeight > 0)
    {
      const GLint openGLViewportX = previousViewport[0] + displayState.viewportX;
      const GLint openGLViewportY = previousViewport[1] + previousViewport[3] - displayState.viewportY - displayState.viewportHeight;
      glViewport(openGLViewportX, openGLViewportY, displayState.viewportWidth, displayState.viewportHeight);

      if (displayState.useScissorRect)
      {
        glEnable(GL_SCISSOR_TEST);
        glScissor(openGLViewportX, openGLViewportY, displayState.viewportWidth, displayState.viewportHeight);
        ++renderedScissorCommands;
      }
      else
      {
        glDisable(GL_SCISSOR_TEST);
      }
    }
    else
    {
      glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
      glDisable(GL_SCISSOR_TEST);
    }

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(displayState.displayX0, displayState.displayX1, displayState.displayY1, displayState.displayY0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    switch (command.kind)
    {
    case LinuxFlashDrawCommand::BeginSubmitMaskCommand:
      BeginLinuxFlashSubmitMask(maskLevel);
      ++renderedMaskCommands;
      continue;

    case LinuxFlashDrawCommand::EndSubmitMaskCommand:
      EndLinuxFlashSubmitMask(maskLevel);
      ++renderedMaskCommands;
      continue;

    case LinuxFlashDrawCommand::BeginUnSubmitMaskCommand:
      BeginLinuxFlashUnsubmitMask(maskLevel);
      ++renderedMaskCommands;
      continue;

    case LinuxFlashDrawCommand::DisableMaskCommand:
      DisableLinuxFlashMask(maskLevel);
      ++renderedMaskCommands;
      continue;

    case LinuxFlashDrawCommand::DrawText:
      SetLinuxOpenGLUiRendererFlashTextStyle(
        command.textPartID,
        command.textTexture,
        command.textWithBevel,
        command.textBevelColor);
      GetUIRenderer()->RenderPart(command.textPartID, ERenderWhat::_2D, false);
      ++renderedCommands;
      continue;

    default:
      break;
    }

    ApplyLinuxFlashBlendMode(command.blendMode);
    if (IsLinuxFlashMappedBlendMode(command.blendMode))
      ++renderedBlendCommands;
    if (command.line)
    {
      ++renderedLineCommands;
      renderedLineVertices += command.vertices.size();
    }
    if (command.scale9Grid)
    {
      ++renderedScale9Commands;
      if (command.textured)
        ++renderedScale9TexturedCommands;
    }

    unsigned int openGLTexture = 0;
    if (command.textured && command.texture)
    {
      command.texture->EnsureOpenGLTexture();
      openGLTexture = command.texture->GetOpenGLTexture();
    }

    if (openGLTexture)
    {
      ++renderedTexturedCommands;
      if (command.gradientFill)
      {
        ++renderedGradientCommands;
        if (command.gradientType == flash::EGradientType::Focal)
          ++renderedFocalGradientCommands;
      }
      if (command.wrapMode == EBitmapWrapMode::REPEAT)
        ++renderedRepeatCommands;
      else
        ++renderedClampCommands;

      glEnable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, openGLTexture);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, command.smoothing ? GL_LINEAR : GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, command.smoothing ? GL_LINEAR : GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, command.wrapMode == EBitmapWrapMode::REPEAT ? GL_REPEAT : GL_CLAMP);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, command.wrapMode == EBitmapWrapMode::REPEAT ? GL_REPEAT : GL_CLAMP);
    }
    else
    {
      glBindTexture(GL_TEXTURE_2D, 0);
      glDisable(GL_TEXTURE_2D);
    }

    glBegin(GL_TRIANGLES);
    for (unsigned int vertex = 0; vertex < command.vertices.size(); ++vertex)
    {
      const LinuxFlashDrawVertex& v = command.vertices[vertex];
      glColor4ub(v.color.R, v.color.G, v.color.B, v.color.A);
      if (openGLTexture)
        glTexCoord2f(v.u, v.v);
      glVertex2f(v.x, v.y);
    }
    glEnd();
    ++renderedCommands;
  }

  if (renderedCommands > 0 || renderedMaskCommands > 0)
    AddLinuxOpenGLUiRendererFlashStats(1, renderedCommands, renderedScissorCommands, renderedMaskCommands, renderedBlendCommands, renderedLineCommands, renderedLineVertices, renderedTexturedCommands, renderedRepeatCommands, renderedClampCommands, renderedScale9Commands, renderedScale9TexturedCommands, renderedGradientCommands, renderedFocalGradientCommands);

  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glPopAttrib();
#else
  (void)firstElement;
  (void)lastElement;
#endif
}

void FlashRenderer::SetResolutionCoefs( float x, float y, float widthScale, float heightScale )
{
  resolutionXCoef = x;
  resolutionYCoef = y;
  this->widthScale = widthScale;
  this->heightScale = heightScale;
}

void FlashRenderer::SetMatrix( const flash::SWF_MATRIX& matrix )
{
  currentMatrix = matrix;
}

void FlashRenderer::SetColorTransform( const flash::SWF_CXFORMWITHALPHA& cx )
{
  currentColorTransform = cx;
}

void FlashRenderer::SetBlendMode( EFlashBlendMode::Enum blendMode )
{
  currentBlendMode = blendMode;
}

void FlashRenderer::SetFillStyleBitmap( IBitmapInfo* bitmapInfo, const flash::SWF_MATRIX& matrix, EBitmapWrapMode::Enum wrapMode, bool primary )
{
  LinuxFlashFillStyle& fillStyle = primary ? primaryFillStyle : secondaryFillStyle;
  fillStyle = LinuxFlashFillStyle();

  LinuxBitmapInfo* bitmap = GetLinuxBitmapInfo(bitmapInfo);
  if (!bitmap || !bitmap->GetTexture())
    return;

  fillStyle.enabled = true;
  fillStyle.smoothing = true;
  fillStyle.gradientFill = bitmap->IsGradientTexture();
  fillStyle.gradientType = bitmap->GetGradientType();
  fillStyle.wrapMode = wrapMode;
  fillStyle.texture = bitmap->GetTexture();
  fillStyle.matrix = matrix;
}

void FlashRenderer::SetLineWidth( float width )
{
  lineWidth = width > 0.0f ? width : 1.0f;
}

void FlashRenderer::SetLineColor( const flash::SWF_RGBA& color )
{
  lineColor = Color(color.r, color.g, color.b, color.a);
}

IBitmapInfo* FlashRenderer::CreateBitmap( int width, int height )
{
  return new LinuxBitmapInfo( width, height );
}

IBitmapInfo* FlashRenderer::CreateBitmapFromTexture( const Texture2DRef& texture )
{
  return new LinuxBitmapInfo( texture );
}

IBitmapInfo* FlashRenderer::CreateBitmapInfoRgba( image::rgba* im, bool repeatable )
{
  return new LinuxBitmapInfo( im, repeatable );
}

IBitmapInfo* FlashRenderer::CreateBitmapFromFile( const nstl::string& filename )
{
  return new LinuxBitmapInfo( Render::Load2DTextureFromFile( filename ) );
}

IBitmapInfo* FlashRenderer::CreateGradientBitmap( const flash::SWF_GRADIENT& gradient )
{
  return new LinuxBitmapInfo( CreateLinuxGradientTexture(gradient), true, gradient.type );
}

void FlashRenderer::BeginDisplay(
  int viewport_x0, int viewport_y0,
  int viewport_width, int viewport_height,
  float x0, float x1, float y0, float y1,
  bool useScissorRect )
{
  currentDisplayState.viewportX = viewport_x0;
  currentDisplayState.viewportY = viewport_y0;
  currentDisplayState.viewportWidth = viewport_width;
  currentDisplayState.viewportHeight = viewport_height;
  currentDisplayState.displayX0 = x0;
  currentDisplayState.displayX1 = x1;
  currentDisplayState.displayY0 = y0;
  currentDisplayState.displayY1 = y1;
  currentDisplayState.useScissorRect = useScissorRect;
  displayActive = true;
  GetUIRenderer()->BeginFlashParts( static_cast<int>(drawCommands.size()) );
}

void FlashRenderer::EndDisplay()
{
  displayActive = false;
  GetUIRenderer()->EndFlashParts( static_cast<int>(drawCommands.size()) );
}

void FlashRenderer::DrawBitmap( IBitmapInfo* bitmapInfo, float width, float height, int uniqueID, bool smoothing )
{
  (void)uniqueID;
  AppendBitmapQuad(bitmapInfo, 0.0f, 0.0f, width, height, bitmapInfo ? bitmapInfo->GetUV1().x : 0.0f, bitmapInfo ? bitmapInfo->GetUV1().y : 0.0f, bitmapInfo ? bitmapInfo->GetUV2().x : 1.0f, bitmapInfo ? bitmapInfo->GetUV2().y : 1.0f, smoothing);
}

void FlashRenderer::DrawBitmapScale9Grid( IBitmapInfo* bitmapInfo, float width, float height, const flash::SWF_RECT& scale9Grid, float aspectX, float aspectY, int uniqueID, bool smoothing )
{
  (void)uniqueID;
  if (!bitmapInfo || bitmapInfo->GetWidth() <= 0 || bitmapInfo->GetHeight() <= 0)
    return;

  const float safeAspectX = fabsf(aspectX) > 0.0001f ? aspectX : 1.0f;
  const float safeAspectY = fabsf(aspectY) > 0.0001f ? aspectY : 1.0f;

  float u[4] = { 0.0f, scale9Grid.X1 / float(bitmapInfo->GetWidth()), scale9Grid.X2 / float(bitmapInfo->GetWidth()), 1.0f };
  float v[4] = { 0.0f, scale9Grid.Y1 / float(bitmapInfo->GetHeight()), scale9Grid.Y2 / float(bitmapInfo->GetHeight()), 1.0f };
  float x[4] = {
    0.0f,
    u[1] * width / safeAspectX,
    (1.0f - (1.0f - u[2]) / safeAspectX) * width,
    width
  };
  float y[4] = {
    0.0f,
    v[1] * height / safeAspectY,
    (1.0f - (1.0f - v[2]) / safeAspectY) * height,
    height
  };

  const float scaleU = bitmapInfo->GetUV2().x - bitmapInfo->GetUV1().x;
  const float scaleV = bitmapInfo->GetUV2().y - bitmapInfo->GetUV1().y;
  for (int i = 0; i < 4; ++i)
  {
    u[i] = bitmapInfo->GetUV1().x + u[i] * scaleU;
    v[i] = bitmapInfo->GetUV1().y + v[i] * scaleV;
  }

  // Match the Windows Flash renderer by drawing the scaled bitmap as a 3x3 grid.
  for (int i = 0; i < 3; ++i)
  {
    for (int j = 0; j < 3; ++j)
      AppendBitmapQuad(bitmapInfo, x[i], y[j], x[i + 1], y[j + 1], u[i], v[j], u[i + 1], v[j + 1], smoothing);
  }
}

void FlashRenderer::DrawTriangleList( ShapeVertex* vertices, int count, int uniqueID )
{
  (void)uniqueID;
  if (!vertices || count <= 0)
  {
    ClearFillStyles();
    return;
  }

  const LinuxFlashFillStyle* fillStyle = 0;
  if (primaryFillStyle.enabled)
    fillStyle = &primaryFillStyle;
  else if (secondaryFillStyle.enabled)
    fillStyle = &secondaryFillStyle;

  LinuxFlashDrawCommand command;
  command.textured = fillStyle && fillStyle->texture;
  command.smoothing = fillStyle ? fillStyle->smoothing : true;
  command.scale9Grid = scale9GridActive;
  command.gradientFill = fillStyle && fillStyle->gradientFill;
  command.gradientType = fillStyle ? fillStyle->gradientType : flash::EGradientType::Linear;
  command.wrapMode = fillStyle ? fillStyle->wrapMode : EBitmapWrapMode::CLAMP;
  command.blendMode = currentBlendMode;
  command.displayState = currentDisplayState;
  if (fillStyle)
    command.texture = fillStyle->texture;
  command.vertices.reserve(count);

  for (int i = 0; i < count; ++i)
  {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    TransformPoint(vertices[i].x, vertices[i].y, &x, &y);
    if (fillStyle)
      TransformFillUV(*fillStyle, vertices[i].x, vertices[i].y, &u, &v);
    command.vertices.push_back(LinuxFlashDrawVertex(x, y, u, v, TransformColor(vertices[i].color)));
  }

  drawCommands.push_back(command);
  ClearFillStyles();
}

void FlashRenderer::DrawLineStrip( const nstl::vector<CVec2>& coords, int uniqueID )
{
  (void)uniqueID;
  if (coords.size() < 2)
    return;

  nstl::vector<CVec2> points;
  points.reserve(coords.size());
  for (unsigned int i = 0; i < coords.size(); ++i)
  {
    float x = 0.0f;
    float y = 0.0f;
    TransformPoint(coords[i].x, coords[i].y, &x, &y);
    if (!points.empty())
    {
      const CVec2& last = points.back();
      const float dx = x - last.x;
      const float dy = y - last.y;
      if (dx * dx + dy * dy <= 0.000001f)
        continue;
    }
    points.push_back(CVec2(x, y));
  }
  if (points.size() < 2)
    return;

  LinuxFlashDrawCommand command;
  command.textured = false;
  command.line = true;
  command.scale9Grid = scale9GridActive;
  command.blendMode = currentBlendMode;
  command.displayState = currentDisplayState;
  command.vertices.reserve((points.size() - 1) * 6);

  const float half = lineWidth * 0.5f;
  const Color color = TransformColor(lineColor);
  nstl::vector<CVec2> left;
  nstl::vector<CVec2> right;
  left.resize(points.size());
  right.resize(points.size());

  // Build one joined screen-space strip so Flash polylines keep continuous corners.
  for (unsigned int i = 0; i < points.size(); ++i)
  {
    CVec2 center = points[i];
    CVec2 normal;
    float scale = half;

    if (i == 0 || i + 1 == points.size())
    {
      const CVec2& a = i == 0 ? points[0] : points[points.size() - 2];
      const CVec2& b = i == 0 ? points[1] : points[points.size() - 1];
      const float dx = b.x - a.x;
      const float dy = b.y - a.y;
      const float len = sqrtf(dx * dx + dy * dy);
      if (len <= 0.0001f)
        continue;
      const CVec2 tangent(dx / len, dy / len);
      normal = CVec2(-tangent.y, tangent.x);
      center.x += (i == 0 ? -tangent.x : tangent.x) * half;
      center.y += (i == 0 ? -tangent.y : tangent.y) * half;
    }
    else
    {
      const CVec2& prev = points[i - 1];
      const CVec2& current = points[i];
      const CVec2& next = points[i + 1];
      const float prevDx = current.x - prev.x;
      const float prevDy = current.y - prev.y;
      const float nextDx = next.x - current.x;
      const float nextDy = next.y - current.y;
      const float prevLen = sqrtf(prevDx * prevDx + prevDy * prevDy);
      const float nextLen = sqrtf(nextDx * nextDx + nextDy * nextDy);
      if (prevLen <= 0.0001f || nextLen <= 0.0001f)
        continue;

      const CVec2 prevNormal(-prevDy / prevLen, prevDx / prevLen);
      const CVec2 nextNormal(-nextDy / nextLen, nextDx / nextLen);
      normal = CVec2(prevNormal.x + nextNormal.x, prevNormal.y + nextNormal.y);
      const float normalLen = sqrtf(normal.x * normal.x + normal.y * normal.y);
      if (normalLen <= 0.0001f)
      {
        normal = nextNormal;
      }
      else
      {
        normal.x /= normalLen;
        normal.y /= normalLen;
        const float dot = normal.x * nextNormal.x + normal.y * nextNormal.y;
        if (fabsf(dot) > 0.2f)
          scale = Clamp(half / dot, -half * 4.0f, half * 4.0f);
      }
    }

    left[i] = CVec2(center.x + normal.x * scale, center.y + normal.y * scale);
    right[i] = CVec2(center.x - normal.x * scale, center.y - normal.y * scale);
  }

  for (unsigned int i = 0; i + 1 < points.size(); ++i)
  {
    command.vertices.push_back(LinuxFlashDrawVertex(left[i].x, left[i].y, 0.0f, 0.0f, color));
    command.vertices.push_back(LinuxFlashDrawVertex(right[i].x, right[i].y, 0.0f, 0.0f, color));
    command.vertices.push_back(LinuxFlashDrawVertex(right[i + 1].x, right[i + 1].y, 0.0f, 0.0f, color));
    command.vertices.push_back(LinuxFlashDrawVertex(left[i].x, left[i].y, 0.0f, 0.0f, color));
    command.vertices.push_back(LinuxFlashDrawVertex(right[i + 1].x, right[i + 1].y, 0.0f, 0.0f, color));
    command.vertices.push_back(LinuxFlashDrawVertex(left[i + 1].x, left[i + 1].y, 0.0f, 0.0f, color));
  }

  if (!command.vertices.empty())
    drawCommands.push_back(command);
}

void FlashRenderer::TransformPoint(float x, float y, float* outX, float* outY) const
{
  *outX = currentMatrix.m_[0][0] * x + currentMatrix.m_[0][1] * y + currentMatrix.m_[0][2];
  *outY = currentMatrix.m_[1][0] * x + currentMatrix.m_[1][1] * y + currentMatrix.m_[1][2];

  if (scale9GridActive)
  {
    *outX = ApplyLinuxFlashScale9GridCoord(*outX, scale9ConstX) + scale9Trans.z;
    *outY = ApplyLinuxFlashScale9GridCoord(*outY, scale9ConstY) + scale9Trans.w;
  }
}

void FlashRenderer::TransformFillUV(const LinuxFlashFillStyle& fillStyle, float x, float y, float* outU, float* outV) const
{
  if (scale9GridActive)
  {
    // Match UIFlashMaterial's scale9 UV source: remapped position before parent translation.
    float sx = currentMatrix.m_[0][0] * x + currentMatrix.m_[0][1] * y + currentMatrix.m_[0][2];
    float sy = currentMatrix.m_[1][0] * x + currentMatrix.m_[1][1] * y + currentMatrix.m_[1][2];
    sx = ApplyLinuxFlashScale9GridCoord(sx, scale9ConstX);
    sy = ApplyLinuxFlashScale9GridCoord(sy, scale9ConstY);

    const float transScaleX = fabsf(scale9Trans.x) > 0.0001f ? scale9Trans.x : 1.0f;
    const float transScaleY = fabsf(scale9Trans.y) > 0.0001f ? scale9Trans.y : 1.0f;
    x = sx / transScaleX;
    y = sy / transScaleY;
  }

  *outU = fillStyle.matrix.m_[0][0] * x + fillStyle.matrix.m_[0][1] * y + fillStyle.matrix.m_[0][2];
  *outV = fillStyle.matrix.m_[1][0] * x + fillStyle.matrix.m_[1][1] * y + fillStyle.matrix.m_[1][2];
}

Color FlashRenderer::TransformColor(const Color& color) const
{
  float red = ClampFlashColorChannel(color.R * currentColorTransform.m_[0][0] + currentColorTransform.m_[0][1]) / 255.0f;
  float green = ClampFlashColorChannel(color.G * currentColorTransform.m_[1][0] + currentColorTransform.m_[1][1]) / 255.0f;
  float blue = ClampFlashColorChannel(color.B * currentColorTransform.m_[2][0] + currentColorTransform.m_[2][1]) / 255.0f;
  float alpha = ClampFlashColorChannel(color.A * currentColorTransform.m_[3][0] + currentColorTransform.m_[3][1]) / 255.0f;

  if (!colorMatrixStack.empty())
  {
    const LinuxFlashColorMatrixState& state = colorMatrixStack.back();
    const SHMatrix& matrix = state.colorMatrix;
    const CVec4 addColor = state.addColor / 255.0f;
    const float sourceRed = red;
    const float sourceGreen = green;
    const float sourceBlue = blue;
    const float sourceAlpha = alpha;

    red =
      sourceRed * matrix._11 +
      sourceGreen * matrix._21 +
      sourceBlue * matrix._31 +
      sourceAlpha * matrix._41 +
      addColor.x;
    green =
      sourceRed * matrix._12 +
      sourceGreen * matrix._22 +
      sourceBlue * matrix._32 +
      sourceAlpha * matrix._42 +
      addColor.y;
    blue =
      sourceRed * matrix._13 +
      sourceGreen * matrix._23 +
      sourceBlue * matrix._33 +
      sourceAlpha * matrix._43 +
      addColor.z;
    alpha =
      sourceRed * matrix._14 +
      sourceGreen * matrix._24 +
      sourceBlue * matrix._34 +
      sourceAlpha * matrix._44 +
      addColor.w;
  }

  return Color(
    ClampFlashColorChannel(red * 255.0f),
    ClampFlashColorChannel(green * 255.0f),
    ClampFlashColorChannel(blue * 255.0f),
    ClampFlashColorChannel(alpha * 255.0f));
}

void FlashRenderer::ClearFillStyles()
{
  primaryFillStyle = LinuxFlashFillStyle();
  secondaryFillStyle = LinuxFlashFillStyle();
}

void FlashRenderer::QueueMaskCommand(LinuxFlashDrawCommand::Kind kind)
{
  LinuxFlashDrawCommand command;
  command.kind = kind;
  command.displayState = currentDisplayState;
  drawCommands.push_back(command);
}

void FlashRenderer::AppendBitmapQuad(
  IBitmapInfo* bitmapInfo,
  float x1,
  float y1,
  float x2,
  float y2,
  float u1,
  float v1,
  float u2,
  float v2,
  bool smoothing)
{
  LinuxBitmapInfo* bitmap = GetLinuxBitmapInfo(bitmapInfo);
  if (!bitmap || !bitmap->GetTexture())
    return;

  float tx1 = 0.0f;
  float ty1 = 0.0f;
  float tx2 = 0.0f;
  float ty2 = 0.0f;
  float tx3 = 0.0f;
  float ty3 = 0.0f;
  float tx4 = 0.0f;
  float ty4 = 0.0f;
  TransformPoint(x1, y1, &tx1, &ty1);
  TransformPoint(x2, y1, &tx2, &ty2);
  TransformPoint(x2, y2, &tx3, &ty3);
  TransformPoint(x1, y2, &tx4, &ty4);

  LinuxFlashDrawCommand command;
  command.textured = true;
  command.smoothing = smoothing;
  command.scale9Grid = scale9GridActive;
  command.gradientFill = bitmap->IsGradientTexture();
  command.gradientType = bitmap->GetGradientType();
  command.wrapMode = EBitmapWrapMode::CLAMP;
  command.blendMode = currentBlendMode;
  command.displayState = currentDisplayState;
  command.texture = bitmap->GetTexture();
  command.vertices.reserve(6);

  const Color color = TransformColor(Color(255, 255, 255, 255));
  command.vertices.push_back(LinuxFlashDrawVertex(tx1, ty1, u1, v1, color));
  command.vertices.push_back(LinuxFlashDrawVertex(tx2, ty2, u2, v1, color));
  command.vertices.push_back(LinuxFlashDrawVertex(tx3, ty3, u2, v2, color));
  command.vertices.push_back(LinuxFlashDrawVertex(tx1, ty1, u1, v1, color));
  command.vertices.push_back(LinuxFlashDrawVertex(tx3, ty3, u2, v2, color));
  command.vertices.push_back(LinuxFlashDrawVertex(tx4, ty4, u1, v2, color));
  drawCommands.push_back(command);
}

void FlashRenderer::SetMorph( float rate )
{
  (void)rate;
}

void FlashRenderer::SetScale9Grid( const CVec4& constX, const CVec4& constY, const CVec4& trans )
{
  scale9GridActive = true;
  scale9ConstX = constX;
  scale9ConstY = constY;
  scale9Trans = trans;
}

void FlashRenderer::ResetScale9Grid()
{
  scale9GridActive = false;
}

void FlashRenderer::BeginSubmitMask()
{
  QueueMaskCommand(LinuxFlashDrawCommand::BeginSubmitMaskCommand);
}

void FlashRenderer::EndSubmitMask()
{
  QueueMaskCommand(LinuxFlashDrawCommand::EndSubmitMaskCommand);
}

void FlashRenderer::BeginUnSubmitMask()
{
  QueueMaskCommand(LinuxFlashDrawCommand::BeginUnSubmitMaskCommand);
}

void FlashRenderer::DisableMask()
{
  QueueMaskCommand(LinuxFlashDrawCommand::DisableMaskCommand);
}

void FlashRenderer::BeginColorMatrix( const SHMatrix& colorMatrix, const CVec4& addColor )
{
  colorMatrixStack.push_back(LinuxFlashColorMatrixState(colorMatrix, addColor));
}

void FlashRenderer::EndColorMatrix()
{
  if (!colorMatrixStack.empty())
    colorMatrixStack.pop_back();
}

void FlashRenderer::RenderText( int partID )
{
  if (partID < 0)
    return;

  LinuxFlashDrawCommand command;
  command.kind = LinuxFlashDrawCommand::DrawText;
  command.displayState = currentDisplayState;
  command.blendMode = currentBlendMode;
  command.textPartID = partID;
  command.textTexture = nextTextTexture;
  command.textWithBevel = nextTextWithBevel;
  command.textBevelColor = nextTextBevelColor;
  drawCommands.push_back(command);
}

void FlashRenderer::RenderTextBevel( bool withBevel, const flash::SWF_RGBA& color, Texture* fontTexture )
{
  nextTextWithBevel = withBevel;
  nextTextBevelColor = Color(color.r, color.g, color.b, color.a);
  nextTextTexture = Texture2DRef(dynamic_cast<Texture2D*>(fontTexture));
}

void FlashRenderer::ClearCaches()
{
  colorMatrixStack.clear();
  nextTextTexture = Texture2DRef();
  nextTextWithBevel = false;
  nextTextBevelColor = Color(0, 0, 0, 255);
}

void FlashRenderer::DebugNextBatch()
{
}

} // namespace Render

NI_DEFINE_REFCOUNT( Render::IBitmapInfo );

#else

#include "../UI/Flash/GameSWFIntegration/Image.h"

#include "batch.h"
#include "FlashRenderer.h"
#include "MaterialSpec.h"
#include "smartrenderer.h"
#include "uirenderer.h"
#include "GlobalMasks.h"
#include "ImmediateRenderer.h"
#include "UITextureCacheInterface.h"

#include "../System/InlineProfiler.h"
#include "../System/RandomGen.h"

namespace
{
  static NDebug::DebugVar<int> flash_VerticesCount( "Flash Vertices", "Flash" );
  static NDebug::DebugVar<int> flash_VBSize( "Flash VB Size", "Flash" );
  static NDebug::DebugVar<int> flash_VBShapeSize( "Flash VBShape Size", "Flash" );
  static NDebug::DebugVar<int> flash_BatchesCount( "Flash Batches", "Flash" );
  static NDebug::DebugVar<int> flash_DrawCalls( "Flash Draw Calls", "Flash" );
  static NDebug::DebugVar<int> flash_TargetSwitch( "Flash Target Switch", "Flash" );
  static NDebug::DebugVar<int> flash_BitmapCount( "Bitmap Count", "Flash" );
  static NDebug::DebugVar<int> flash_BuiltinBitmapSize( "Builtin Bitmap Size", "Flash" );

  bool gSkipFlashRender = false;
  REGISTER_DEV_VAR( "skip_flash_render", gSkipFlashRender, STORAGE_NONE );

  static bool flashTest = false;
  REGISTER_DEV_VAR( "flashTest", flashTest, STORAGE_NONE );
}

namespace Render
{

#define TEXTURE_MATRIX  VIEWPROJECTION

static const int MAX_VERTEX_COUNT = 1024 * 48;
static const int MAX_BATCHES = 4096 * 2; // Total allocated batch size ~ 2 Mb 

// bitmap_info_d3d declaration
class BitmapInfoD3D : public IBitmapInfo, public BaseObjectST
{
  NI_DECLARE_REFCOUNT_CLASS_2( BitmapInfoD3D, IBitmapInfo, BaseObjectST );

public:

  int m_width;
  int m_height;
  int m_lineID; // for linear gradients
  Texture2DRef m_texture;

  CVec2 uv1;
  CVec2 uv2;

  BitmapInfoD3D() :
    m_width(0),
    m_height(0),
    m_lineID(0),
    uv1( 0.f, 0.f ),
    uv2( 1.f, 1.f )
  {
  }

  BitmapInfoD3D( const flash::SWF_GRADIENT& gradient ) :
    m_lineID(0),
    uv1( 0.f, 0.f ),
    uv2( 1.f, 1.f )
  {
    m_texture = GetUIRenderer()->GetTextureCache()->GetGradientTexture( gradient );

    m_width = m_texture->GetWidth();
    m_height = m_texture->GetHeight();

    flash_BitmapCount.AddValue( 1 );
  }

  BitmapInfoD3D( const Texture2DRef& _texture ) :
    m_texture( _texture ),
    uv1( 0.f, 0.f ),
    uv2( 1.f, 1.f )
  { 
    m_width = m_texture->GetWidth();
    m_height = m_texture->GetHeight();

    flash_BitmapCount.AddValue( 1 );
  }


  BitmapInfoD3D( image::rgba* im, bool repeatable ) :
    m_width(im->m_width),
    m_height(im->m_height),
    uv1( 0.f, 0.f ),
    uv2( 1.f, 1.f )
  {
    m_texture = GetUIRenderer()->GetTextureCache()->PlaceImageToAtlas( im, uv1, uv2, repeatable );
  }

  BitmapInfoD3D( const nstl::string& filename ) :
    uv1( 0.f, 0.f ),
    uv2( 1.f, 1.f )
  {
    m_texture = Render::Load2DTextureFromFile( filename );

    if ( m_texture )
    {
      m_width = m_texture->GetWidth();
      m_height = m_texture->GetHeight();
    }

    flash_BitmapCount.AddValue( 1 );
  }

  BitmapInfoD3D( int width, int height ):
    m_width(width),
    m_height(height),
    uv1( 0.f, 0.f ),
    uv2( 1.f, 1.f )
  {
    m_texture = Render::CreateTexture2D( width, height, 1, RENDER_POOL_MANAGED, FORMAT_A8R8G8B8 );

    flash_BitmapCount.AddValue( 1 );
  }

  ~BitmapInfoD3D()
  {
    flash_BitmapCount.AddValue( -1 );
  }

  virtual int GetWidth() const { return m_width; }
  virtual int GetHeight() const { return m_height; }
  virtual const CVec2& GetUV1() const { return uv1; }
  virtual const CVec2& GetUV2() const { return uv2; }

  virtual IBitmapInfo* Clone() 
  {
    BitmapInfoD3D * cloneBmp = new BitmapInfoD3D();
    cloneBmp->m_texture = Render::CreateTexture2D( m_width, m_height, 1, RENDER_POOL_MANAGED, FORMAT_A8R8G8B8 );

    LockedRect lockedRectDest = cloneBmp->m_texture->LockRect( 0, LOCK_DEFAULT );
    LockedRect lockedRectSource = m_texture->LockRect( 0, LOCK_DEFAULT );

    memcpy( lockedRectDest.data, lockedRectSource.data, m_width * m_height * 4 );

    cloneBmp->m_texture->UnlockRect( 0 );
    m_texture->UnlockRect( 0 );

    cloneBmp->m_width = m_width;
    cloneBmp->m_height = m_height;

    return cloneBmp;
  }

  virtual void Draw( IBitmapInfo* source, const flash::SWF_MATRIX& _matrix, int x1, int y1, int x2, int y2 )
  {
    if ( x1 > x2 )
      return;

    if ( y1 > y2 )
      return;

    x1 = Clamp( x1, 0, source->GetWidth() - 1 ); 
    x2 = Clamp( x2, 0, source->GetWidth() - 1 ); 
    y1 = Clamp( y1, 0, source->GetHeight() - 1 ); 
    y2 = Clamp( y2, 0, source->GetHeight() - 1 ); 

    BitmapInfoD3D* sourceD3D = dynamic_cast<BitmapInfoD3D*>( source );

    if ( !sourceD3D )
      return;

    LockedRect lockedRectDest = m_texture->LockRect( 0, LOCK_DEFAULT );
    LockedRect lockedRectSource = sourceD3D->m_texture->LockRect( 0, LOCK_DEFAULT );
    
    for ( int i = x1; i <= x2 && i < m_width; ++i )
    {
      for ( int j = y1; j <= y2 && j < m_height; ++j )
      {
        int base1 = ( j * m_width + i ) * 4;
        int base2 = ( j * source->GetWidth() + i ) * 4;

        lockedRectDest.data[ base1 + 0 ] = lockedRectSource.data[ base2 + 0 ];
        lockedRectDest.data[ base1 + 1 ] = lockedRectSource.data[ base2 + 1 ];
        lockedRectDest.data[ base1 + 2 ] = lockedRectSource.data[ base2 + 2 ];
        lockedRectDest.data[ base1 + 3 ] = lockedRectSource.data[ base2 + 3 ];
      }
    }

    sourceD3D->m_texture->UnlockRect( 0 );
    m_texture->UnlockRect( 0 );
  }
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
FlashRenderer::FlashRenderer() : 
  numBatches(0),
  numVertices(0),
  numVerticesShape(0),
  numRendered(0),
  numDIP(0),
  numTargetSwitches(0),
  isLocked(false),
  vertexBufferRaw(0),
  vertexBufferShapeRaw(0),
  resolutionXCoef(1.f),
  resolutionYCoef(1.f),
  widthScale(1.f),
  heightScale(1.f),
  maskLevel(0),

  flashMaterial(0),

  lineWidthHalf(40.f),

  flashConstants(1.f, 1.f, 1.f, 1.f),

  nextTextWithBevel(false),
  fontTexture(0),

  currentPixelScale(20.f),
  modelMatrixCached(false),
  colorTransformCached(false)
{
  batches.resize(MAX_BATCHES);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
FlashRenderer::~FlashRenderer()
{
  if ( flashMaterial )
    delete flashMaterial;

  flashMaterial = 0;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::InitVertexDeclaration()
{
  VertexFormatDescriptor formatDescriptor;

  formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 0, VERTEXELEMENTTYPE_FLOAT2, VERETEXELEMENTUSAGE_POSITION, 0) );
  formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 8, VERTEXELEMENTTYPE_FLOAT2, VERETEXELEMENTUSAGE_TEXCOORD, 0) );
  formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 16, VERTEXELEMENTTYPE_D3DCOLOR, VERETEXELEMENTUSAGE_COLOR, 0) );
  formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 20, VERTEXELEMENTTYPE_FLOAT2, VERETEXELEMENTUSAGE_TEXCOORD, 1) );
  formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 28, VERTEXELEMENTTYPE_FLOAT2, VERETEXELEMENTUSAGE_TEXCOORD, 2) );
  formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 36, VERTEXELEMENTTYPE_FLOAT2, VERETEXELEMENTUSAGE_TEXCOORD, 3) );

  vertexDeclaration = SmartRenderer::GetVertexFormatDeclaration( formatDescriptor );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::InitShapeVertexDeclaration()
{
  VertexFormatDescriptor formatDescriptor;

  formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 0, VERTEXELEMENTTYPE_FLOAT2, VERETEXELEMENTUSAGE_POSITION, 0) );
  formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 8, VERTEXELEMENTTYPE_D3DCOLOR, VERETEXELEMENTUSAGE_COLOR, 0) );
  formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 12, VERTEXELEMENTTYPE_FLOAT1, VERETEXELEMENTUSAGE_TEXCOORD, 0) );

  vertexDeclarationShape = SmartRenderer::GetVertexFormatDeclaration( formatDescriptor );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool FlashRenderer::Initialize()
{
  InitVertexDeclaration();
  InitShapeVertexDeclaration();

  dipDescriptor.primitiveType = RENDERPRIMITIVE_TRIANGLESTRIP;

  // Create vertex buffer
  vertexBuffer.Resize( MAX_VERTEX_COUNT * sizeof(FlashVertex) );
  NI_VERIFY( Get( vertexBuffer ), "FlashRenderer: Failed to create vertex buffer!", return false );

  vertexBufferShape.Resize( MAX_VERTEX_COUNT * sizeof(ShapeVertex) );
  NI_VERIFY( Get( vertexBufferShape ), "FlashRenderer: Failed to create shape vertex buffer!", return false );

  // Material
  flashMaterial =  static_cast<BaseMaterial*>( Render::CreateRenderMaterial( NDb::UIFlashMaterial::typeId ) );

  // Bevel texture
  bevelTexture = Render::CreateTexture2D( 4, 4, 1, RENDER_POOL_MANAGED, FORMAT_L8 );
  LockedRect lockedRect = bevelTexture->LockRect( 0, LOCK_DEFAULT );
  unsigned char* data = reinterpret_cast<unsigned char*>( lockedRect.data );

  const float bevelConst = 0.17421356f;

#define BIAS_BEVEL( val ) unsigned char( ( (val + 1.f) / 2.f ) * 255.f );

  data[ 0 * 4 + 0 ] = BIAS_BEVEL( bevelConst );
  data[ 0 * 4 + 1 ] = BIAS_BEVEL( 0.f );
  data[ 0 * 4 + 2 ] = BIAS_BEVEL( bevelConst );
  data[ 0 * 4 + 3 ] = BIAS_BEVEL( bevelConst );

  data[ 1 * 4 + 0 ] = BIAS_BEVEL( 0.f );
  data[ 1 * 4 + 1 ] = BIAS_BEVEL( -1.f );
  data[ 1 * 4 + 2 ] = BIAS_BEVEL( 0.f );
  data[ 1 * 4 + 3 ] = BIAS_BEVEL( 0.f );

  data[ 2 * 4 + 0 ] = BIAS_BEVEL( 0.f );
  data[ 2 * 4 + 1 ] = BIAS_BEVEL( -1.f );
  data[ 2 * 4 + 2 ] = BIAS_BEVEL( 0.f );
  data[ 2 * 4 + 3 ] = BIAS_BEVEL( 0.f );

  data[ 3 * 4 + 0 ] = BIAS_BEVEL( bevelConst );
  data[ 3 * 4 + 1 ] = BIAS_BEVEL( 0.f );
  data[ 3 * 4 + 2 ] = BIAS_BEVEL( bevelConst );
  data[ 3 * 4 + 3 ] = BIAS_BEVEL( bevelConst );

#undef BIAS_BEVEL

  bevelTexture->UnlockRect( 0 );

  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::Release()
{
  vertexDeclaration = 0;
  vertexDeclarationShape = 0;

  vertexBuffer.Reset();
  vertexBufferShape.Reset();
  vertexBufferTexture.Reset();

  batches.clear();

  if ( flashMaterial )
	{
    delete flashMaterial;
		flashMaterial = 0;
	}

  bevelTexture = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::BeginQueue()
{
  maskLevel = 0;
  numVertices = 0;
  numVerticesShape = 0;
  numBatches = 0;
  numRendered = 0;
  numDIP = 0;
  numTargetSwitches = 0;
  batches[numBatches].Clear();
  vertexBufferRaw = LockVB<FlashVertex>( Get( vertexBuffer ), D3DLOCK_DISCARD );
  vertexBufferShapeRaw = LockVB<ShapeVertex>( Get( vertexBufferShape ), D3DLOCK_DISCARD );
  isLocked = true;
  colorMatrixStack.clear();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::EndQueue()
{
  NI_ASSERT( isLocked, "Buffer is not locked!" );
  isLocked = false;

  vertexBuffer->Unlock();
  vertexBufferRaw = 0;
  vertexBufferShape->Unlock();
  vertexBufferShapeRaw = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::BreakQueue()
{

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::StartFrame()
{
  flash_VBSize.SetValue( numVertices );
  flash_VBShapeSize.SetValue( numVerticesShape );
  flash_BatchesCount.SetValue( numBatches );
  flash_VerticesCount.SetValue( numRendered );
  flash_DrawCalls.SetValue( numDIP );
  flash_TargetSwitch.SetValue( numTargetSwitches );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::SetResolutionCoefs( float _x, float _y, float _widthScale, float _heightScale )
{
  resolutionXCoef = _x;
  resolutionYCoef = _y;
  widthScale = _widthScale;
  heightScale = _heightScale;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::PreapreStates()
{
  flashConstants.x = resolutionXCoef;
  GetRenderer()->SetVertexShaderConstantsVector4( VSHADER_LOCALCONST7, flashConstants );

  Render::GetStatesManager()->SetSampler(2, Render::SamplerState::PRESET_CLAMP_BILINEAR(), bevelTexture );

  Render::GetStatesManager()->SetStateDirect( D3DRS_CULLMODE, D3DCULL_NONE );

  GetDevice()->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::PrepareTexture( BatchTexture& batch, bool primary )
{
  if ( batch.useDiffuse )
  {
    batch.useDiffuse = false;
    NDb::SamplerState sampleState;
    sampleState.mipFilter = NDb::MIPFILTERTYPE_NONE;
    sampleState.magFilter = batch.smoothing ? NDb::MAGFILTERTYPE_LINEAR : NDb::MAGFILTERTYPE_POINT;
    sampleState.minFilter = batch.smoothing ? NDb::MINFILTERTYPE_LINEAR : NDb::MINFILTERTYPE_POINT;
    sampleState.addressU = (batch.textureMode == EBitmapWrapMode::REPEAT) ? NDb::TEXTUREADDRESSTYPE_WRAP : NDb::TEXTUREADDRESSTYPE_CLAMP;
    sampleState.addressV = sampleState.addressU;
    sampleState.addressW = sampleState.addressU;

    if ( primary )
    {
      flashMaterial->SetUseDiffuse( NDb::BOOLEANPIN_PRESENT );
      flashMaterial->SetScale9GridBitmap( batch.scale9Grid ? NDb::BOOLEANPIN_PRESENT : NDb::BOOLEANPIN_NONE );
      flashMaterial->GetDiffuseMap()->SetTexture( batch.texture );
      flashMaterial->GetDiffuseMap()->SetSamplerState( sampleState );
      GetRenderer()->SetVertexShaderConstantsVector4( VSHADER_LOCALCONST9 + 1, batch.matrixRow1 );
      GetRenderer()->SetVertexShaderConstantsVector4( VSHADER_LOCALCONST9 + 2, batch.matrixRow2 );
    }
    else
    {
      flashMaterial->SetUseDiffuse2( NDb::BOOLEANPIN_PRESENT );
      flashMaterial->GetDiffuseMap2()->SetTexture( batch.texture );
      flashMaterial->GetDiffuseMap2()->SetSamplerState( sampleState );
      GetRenderer()->SetVertexShaderConstantsVector4( VSHADER_LOCALCONST9 + 3, batch.matrixRow1 );
      GetRenderer()->SetVertexShaderConstantsVector4( VSHADER_LOCALCONST9 + 4, batch.matrixRow2 );
    }
  }
  else
  {
    if ( primary )
    {
      flashMaterial->SetScale9GridBitmap( NDb::BOOLEANPIN_NONE );
      flashMaterial->SetUseDiffuse( NDb::BOOLEANPIN_NONE );
    }
    else
    {
      flashMaterial->SetUseDiffuse2( NDb::BOOLEANPIN_NONE );
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::ChangeMatricies( Batch& nextBatch )
{
  if ( nextBatch.needChangeView )
  {
    nextBatch.needChangeView = false;

    GetRenderer()->SetVertexShaderConstantsMatrix( VIEW, nextBatch.viewMatrix );
    currentPixelScale = nextBatch.pixelScale;

    GetDevice()->SetRenderState( D3DRS_STENCILENABLE, FALSE );
    GetDevice()->SetRenderState( D3DRS_STENCILFUNC, D3DCMP_ALWAYS );
    GetDevice()->SetRenderState( D3DRS_STENCILREF, 0 );
  }

  if ( nextBatch.needChangeModelMatrix )
  {
    nextBatch.needChangeModelMatrix = false;

    float xScale = fabs( nextBatch.modelMatrix.GetXAxis3() );
    float yScale = fabs( nextBatch.modelMatrix.GetYAxis3() );
    float scale = 0.6f * currentPixelScale / sqrt( xScale * yScale );
    flashConstants.y = scale;

    GetRenderer()->SetVertexShaderConstantsMatrix( WORLD, nextBatch.modelMatrix );
  }

  flashConstants.z = nextBatch.morphRate;
  GetRenderer()->SetVertexShaderConstantsVector4( VSHADER_LOCALCONST7, flashConstants );
  GetRenderer()->SetPixelShaderConstantsVector4( PSHADER_LOCALCONST7, flashConstants );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::Render( int _firstElement, int _lastElement, const Render::Texture2DRef& pMainRT0 , const Render::Texture2DRef& pMainRT0Copy )
{
  if ( gSkipFlashRender )
    return;

  SetScreenToTextureTransform( pMainRT0 );
  PreapreStates();

  Render::SamplerState samplerState = Render::SamplerState::PRESET_CLAMP_BILINEAR();

  bool wasLine = false;
  bool wasText = false;

  EFlashBlendMode::Enum lastBlendMode = EFlashBlendMode::NORMAL;

  flashMaterial->SetUseDiffuse( NDb::BOOLEANPIN_NONE );
  flashMaterial->SetShaderForLines( NDb::BOOLEANPIN_NONE );

  for ( int i =  _firstElement; i < _lastElement && i < numBatches; ++i )
  {
    Batch& nextBatch = batches[i];

    if ( wasLine ^ nextBatch.isLine )
    {  
      if ( nextBatch.isLine )
        flashMaterial->SetShaderForLines( NDb::BOOLEANPIN_PRESENT );
      else
        flashMaterial->SetShaderForLines( NDb::BOOLEANPIN_NONE );

      wasLine = nextBatch.isLine;
    }

    if ( wasText ^ nextBatch.isText )
    {
      if ( nextBatch.isText )
        GetUIRenderer()->PrepareRender();//FromFlash();
      else
        PreapreStates();

      wasText = nextBatch.isText;
    }

    PrepareTexture( nextBatch.diffuse1, true );
    PrepareTexture( nextBatch.diffuse2, false );

    ChangeMatricies( nextBatch );

    if ( nextBatch.needChandeCXForm )
    {
      nextBatch.needChandeCXForm = false;
      GetRenderer()->SetPixelShaderConstantsHDRColor( PSHADER_LOCALCONST0, nextBatch.multColorCXF );
      GetRenderer()->SetPixelShaderConstantsHDRColor( PSHADER_LOCALCONST1, nextBatch.addColorCXF );
    }

    bool beginMask = false;

    if ( nextBatch.beginSubmitMask )
    {
      beginMask = true;
      nextBatch.beginSubmitMask = false;
      BeginSubmitMaskApply();
    }

    if ( nextBatch.endSubmitMask )
    {
      nextBatch.endSubmitMask = false;
      EndSubmitMaskApply();
    }

    if ( nextBatch.beginUnsubmitMask )
    {
      beginMask = true;
      nextBatch.beginUnsubmitMask = false;
      BeginUnSubmitMaskApply();
    }

    if ( nextBatch.disbaleMask )
    {
      nextBatch.disbaleMask = false;
      DisableMaskApply();
    }

    if ( nextBatch.isText )
    {
      BaseMaterial* baseMaterial = GetUIRenderer()->GetPartMaterial( nextBatch.textPartID, ERenderWhat::_2D );

      if ( baseMaterial )
        PrepareFontMaterial( baseMaterial, nextBatch.hasBevel, nextBatch.bevelColor );

      GetUIRenderer()->RenderPart( nextBatch.textPartID, ERenderWhat::_2D, beginMask ); 

      continue;
    }

    if ( nextBatch.changeBlendMode )
      lastBlendMode = nextBatch.blendMode;

    if ( nextBatch.colorMatrixBegin )
    {
      flashMaterial->SetUseColorFilter( NDb::BOOLEANPIN_PRESENT );

      GetRenderer()->SetPixelShaderConstantsVector4( PSHADER_LOCALCONST8, nextBatch.colorAdd );
      GetRenderer()->SetPixelShaderConstantsMatrix( PSHADER_LOCALCONST9, nextBatch.colorMatrix );

      ColorMatrixElement& elem = colorMatrixStack.push_back();
      elem.colorAdd = nextBatch.colorAdd;
      elem.colorMatrix = nextBatch.colorMatrix;
    }

    if ( nextBatch.useScale9Grid )
    {
      flashMaterial->SetScale9Grid( NDb::BOOLEANPIN_PRESENT );
      GetRenderer()->SetVertexShaderConstantsVector4( VSHADER_LOCALCONST9 + 5, nextBatch.scaleConstX );
      GetRenderer()->SetVertexShaderConstantsVector4( VSHADER_LOCALCONST9 + 6, nextBatch.scaleConstY );
      GetRenderer()->SetVertexShaderConstantsVector4( VSHADER_LOCALCONST9 + 7, nextBatch.transScale9 );
    }

    if ( nextBatch.clearScale9Grid )
      flashMaterial->SetScale9Grid( NDb::BOOLEANPIN_NONE );

    if ( nextBatch.colorMatrixEnd )
    {
      NI_ASSERT( !colorMatrixStack.empty(), "Wrong ColorMatrix sequence" );

      if ( !colorMatrixStack.empty() )
        colorMatrixStack.pop_back();

      if ( !colorMatrixStack.empty() )
      {
        ColorMatrixElement& elem = colorMatrixStack.back();

        GetRenderer()->SetPixelShaderConstantsVector4( PSHADER_LOCALCONST8, elem.colorAdd );
        GetRenderer()->SetPixelShaderConstantsMatrix( PSHADER_LOCALCONST9, elem.colorMatrix );
      }
      else
      {
        flashMaterial->SetUseColorFilter( NDb::BOOLEANPIN_NONE );
      }
    }

    flashMaterial->SetMorphShapes( nextBatch.morph ? NDb::BOOLEANPIN_PRESENT : NDb::BOOLEANPIN_NONE );

    if ( nextBatch.visible )
    {
      NI_VERIFY( nextBatch.prepareFunc, "Wrong prepare function", continue );

      (this->*(nextBatch.prepareFunc))( nextBatch ); 

      if ( NeedToCopyBackground( lastBlendMode ) )
      {
        SmartRenderer::BindRenderTargetColor( 0, pMainRT0Copy->GetSurface( 0 ) );
        SmartRenderer::BindTexture( 3, pMainRT0 );

        flashMaterial->SetFlashBlendModePin( NDb::FLASHBLENDMODEPIN_COPY_BACKGRUOND );
        flashMaterial->PrepareRenderer();

        GetDevice()->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
        GetDevice()->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        GetDevice()->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);

        SmartRenderer::DrawPrimitive( nextBatch.dip );
        numRendered += nextBatch.dip.numVertices;
        numDIP++;
        numTargetSwitches++;

        SmartRenderer::BindRenderTargetColor( 0, pMainRT0->GetSurface( 0 ) );
        SmartRenderer::BindTexture( 3, pMainRT0Copy );
      }

      ApplyBlendMode( lastBlendMode );

      SmartRenderer::DrawPrimitive( nextBatch.dip );
      numRendered += nextBatch.dip.numVertices;
      numDIP++;
    }
  }

  // $TODO check
  GetDevice()->SetRenderState( D3DRS_BLENDOP, D3DBLENDOP_ADD );
  GetDevice()->SetRenderState( D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD );

  GetStatesManager()->SetStencilState(STENCILSTATE_INVALID);
  GetDevice()->SetRenderState( D3DRS_STENCILENABLE, FALSE );
  GetDevice()->SetRenderState( D3DRS_COLORWRITEENABLE, 0x0000000F );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::PrepareShape( const Batch& batch )
{
  SmartRenderer::BindVertexDeclaration( vertexDeclarationShape );
  SmartRenderer::BindVertexBuffer( 0, Get(vertexBufferShape), sizeof(ShapeVertex)	);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::PrepareLine( const Batch& batch )
{
  SmartRenderer::BindVertexDeclaration( vertexDeclaration );
  SmartRenderer::BindVertexBuffer( 0, Get(vertexBuffer), sizeof(FlashVertex)	);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::PrepareBitmap( const Batch& batch )
{
  SmartRenderer::BindVertexDeclaration( vertexDeclaration );
  SmartRenderer::BindVertexBuffer( 0, Get(vertexBuffer), sizeof(FlashVertex)	);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::ApplyBlendMode( EFlashBlendMode::Enum blendMode )
{
  flashMaterial->SetFlashBlendModePin( NDb::FLASHBLENDMODEPIN__NORMAL );

  switch ( blendMode )
  {
  case EFlashBlendMode::ADD :
    flashMaterial->SetFlashBlendModePin( NDb::FLASHBLENDMODEPIN_ADD );
    break;
  case EFlashBlendMode::INVERT :
    flashMaterial->SetFlashBlendModePin( NDb::FLASHBLENDMODEPIN_INVERT );
    break;
  case EFlashBlendMode::DARKEN :
    flashMaterial->SetFlashBlendModePin( NDb::FLASHBLENDMODEPIN_DARKEN );
    break;
  case EFlashBlendMode::MULTIPLY :
    flashMaterial->SetFlashBlendModePin( NDb::FLASHBLENDMODEPIN_MULTIPLY );
    break;
  case EFlashBlendMode::_DIFFERENCE :
    flashMaterial->SetFlashBlendModePin( NDb::FLASHBLENDMODEPIN__DIFFERENCE );
    break;
  case EFlashBlendMode::OVERLAY :
    flashMaterial->SetFlashBlendModePin( NDb::FLASHBLENDMODEPIN_OVERLAY );
    break;
  case EFlashBlendMode::HARDLIGHT :
    flashMaterial->SetFlashBlendModePin( NDb::FLASHBLENDMODEPIN_HARDLIGHT );
    break;
  case EFlashBlendMode::SCREEN :
    flashMaterial->SetFlashBlendModePin( NDb::FLASHBLENDMODEPIN_SCREEN );
    break;
  }

  flashMaterial->PrepareRenderer();

  GetDevice()->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);

  switch ( blendMode )
  {
  case EFlashBlendMode::ADD :
     GetDevice()->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
     GetDevice()->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    break;
  case EFlashBlendMode::MULTIPLY :
     GetDevice()->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
     GetDevice()->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
    break;
  case EFlashBlendMode::DARKEN :
     GetDevice()->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
     GetDevice()->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
     GetDevice()->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_MIN);
    break;
  case EFlashBlendMode::SCREEN :
    GetDevice()->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_INVDESTCOLOR);
    GetDevice()->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    break;
  case EFlashBlendMode::LIGHTEN :
    GetDevice()->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    GetDevice()->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    GetDevice()->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_MAX);
    break;
  case EFlashBlendMode::SUBTRACT :
    GetDevice()->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    GetDevice()->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    GetDevice()->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_REVSUBTRACT);
    break;
  case EFlashBlendMode::INVERT :
    GetDevice()->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_INVDESTCOLOR);
    GetDevice()->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    break;

  case EFlashBlendMode::_DIFFERENCE :
  case EFlashBlendMode::OVERLAY :
  case EFlashBlendMode::HARDLIGHT :
    GetDevice()->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    GetDevice()->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    break;

  default:
     GetDevice()->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
     GetDevice()->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    break;
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool FlashRenderer::NeedToCopyBackground( EFlashBlendMode::Enum blendMode ) const
{
  switch ( blendMode )
  {
  case EFlashBlendMode::_DIFFERENCE :
  case EFlashBlendMode::HARDLIGHT :
  case EFlashBlendMode::OVERLAY :
    return true;
  }

  return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::SetScreenToTextureTransform( const Render::Texture2DRef& pMainRT0 )
{
  CVec4 screenScaleOffset;
  screenScaleOffset.x = 1.0f / pMainRT0->GetWidth();
  screenScaleOffset.y = 1.0f / pMainRT0->GetHeight();
  screenScaleOffset.z = 0.5f * screenScaleOffset.x;
  screenScaleOffset.w = 0.5f * screenScaleOffset.y;

  GetRenderer()->SetPixelShaderConstantsVector4(SCREEN_TO_TEXTURE, screenScaleOffset);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
IBitmapInfo* FlashRenderer::CreateBitmap( int width, int height )
{
  return new BitmapInfoD3D( width, height );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
IBitmapInfo* FlashRenderer::CreateBitmapFromTexture( const Texture2DRef& _texture )
{
  return new BitmapInfoD3D( _texture );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
IBitmapInfo* FlashRenderer::CreateBitmapInfoRgba( image::rgba* im, bool repeatable )
{
//  return new BitmapInfoD3D( "Tech/checker256.dds" );
  return new BitmapInfoD3D( im, repeatable );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
IBitmapInfo* FlashRenderer::CreateBitmapFromFile( const nstl::string& filename )
{
  return new BitmapInfoD3D( filename );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
IBitmapInfo* FlashRenderer::CreateGradientBitmap( const flash::SWF_GRADIENT& gradient )
{
  return new BitmapInfoD3D( gradient );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::BeginDisplay(
                            int viewport_x0, int viewport_y0,
                            int viewport_width, int viewport_height,
                            float x0, float x1, float y0, float y1,
                            bool useScissorRect )
{
  if ( numBatches >= MAX_BATCHES )
    return;

  modelMatrixCached = false;
  colorTransformCached = false;

  GetUIRenderer()->BeginFlashParts( numBatches );

  batches[ numBatches ].needChangeView = true;
  Identity( &batches[ numBatches ].viewMatrix );

  batches[ numBatches ].viewMatrix._11 = 1.0f / (x1 - x0);
  batches[ numBatches ].viewMatrix._22 = -1.0f / (y1 - y0);
  batches[ numBatches ].viewMatrix._41 = -((x1 + x0) / (x1 - x0));
  batches[ numBatches ].viewMatrix._42 = ((y1 + y0) / (y1 - y0));

  SHMatrix windowMatrix;
  Identity( &windowMatrix );

  float viewportWidth = static_cast<float>( viewport_width );
  float viewportHeight = static_cast<float>( viewport_height );
  float viewportX0 = static_cast<float>( viewport_x0 );
  float viewportY0 = static_cast<float>( viewport_y0 );

  windowMatrix._11 = viewportWidth * resolutionXCoef;
  windowMatrix._22 = viewportHeight * resolutionYCoef;
  windowMatrix._41 = -1.0f + viewportX0 * resolutionXCoef + viewportWidth * resolutionXCoef;
  windowMatrix._42 = 1.0f - viewportY0 * resolutionYCoef - viewportHeight * resolutionYCoef;

  batches[ numBatches ].viewMatrix = batches[ numBatches ].viewMatrix * windowMatrix;
  batches[ numBatches ].pixelScale = (x1 - x0) / ( viewportWidth * widthScale );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void	FlashRenderer::EndDisplay()
{
  GetUIRenderer()->EndFlashParts( numBatches );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::SetMatrix( const flash::SWF_MATRIX& m )
{
  if ( numBatches >= MAX_BATCHES )
    return;

  if ( modelMatrixCached && m == lastModelMatrix )
    return;

  Batch& batch = batches[ numBatches ];

  batch.needChangeModelMatrix = true;

  batch.modelMatrix._11 = m.m_[0][0]; batch.modelMatrix._12 = m.m_[1][0];
  batch.modelMatrix._21 = m.m_[0][1]; batch.modelMatrix._22 = m.m_[1][1];
  batch.modelMatrix._41 = m.m_[0][2]; batch.modelMatrix._42 = m.m_[1][2];

  lastModelMatrix = m;
  modelMatrixCached = true;
}

void FlashRenderer::SetColorTransform( const flash::SWF_CXFORMWITHALPHA& cx )
{
  if ( numBatches >= MAX_BATCHES )
    return;

  if ( colorTransformCached && cx == lastColorTransform )
    return;

  Batch& batch = batches[ numBatches ];

  batch.needChandeCXForm = true;

  batch.multColorCXF.R = cx.m_[0][0];
  batch.multColorCXF.G = cx.m_[1][0];
  batch.multColorCXF.B = cx.m_[2][0];
  batch.multColorCXF.A = cx.m_[3][0];

  batch.addColorCXF.R = cx.m_[0][1] / 255.f;
  batch.addColorCXF.G = cx.m_[1][1] / 255.f;
  batch.addColorCXF.B = cx.m_[2][1] / 255.f;
  batch.addColorCXF.A = cx.m_[3][1] / 255.f;

  lastColorTransform = cx;
  colorTransformCached = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::SetBlendMode( EFlashBlendMode::Enum blendMode )
{
  if ( numBatches >= MAX_BATCHES )
    return;

  batches[ numBatches ].changeBlendMode = true;
  batches[ numBatches ].blendMode = blendMode;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::DrawTriangleList( ShapeVertex* vertices, int vertex_count, int uniqueID )
{
  NI_PROFILE_FUNCTION

  if(!vertexBufferShapeRaw)
    return;

  if ( numBatches >= MAX_BATCHES )
    return;

  if ( numVerticesShape + vertex_count > MAX_VERTEX_COUNT )
    return;

  Batch& batch = batches[ numBatches ];

  batch.prepareFunc = &FlashRenderer::PrepareShape;

  memcpy( &vertexBufferShapeRaw[numVerticesShape], vertices, vertex_count * sizeof( ShapeVertex ) );

  batch.dip.primitiveType = RENDERPRIMITIVE_TRIANGLELIST;
  batch.dip.baseVertexIndex = numVerticesShape;	
  batch.dip.startIndex = numVerticesShape;	// $TODO error in Render
  batch.dip.numVertices = vertex_count;
  batch.dip.primitiveCount = (vertex_count / 3);

  numVerticesShape += vertex_count;

  NextBatch();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::SetMorph( float rate )
{
  if ( numBatches >= MAX_BATCHES )
    return;

  Batch& batch = batches[ numBatches ];

  batch.morph = true;
  batch.morphRate = rate;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::SetScale9Grid( const CVec4& constX, const CVec4& constY, const CVec4& trans )
{
  if ( numBatches >= MAX_BATCHES )
    return;

  Batch& batch = batches[ numBatches ];

  batch.useScale9Grid = true;
  batch.scaleConstX = constX;
  batch.scaleConstY = constY;
  batch.transScale9 = trans;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::ResetScale9Grid()
{
  if ( numBatches >= MAX_BATCHES )
    return;

  batches[ numBatches ].clearScale9Grid = true;
  batches[ numBatches ].visible = false;

  NextBatch();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::DrawLineStrip( const nstl::vector<CVec2>& _coords, int uniqueID )
{
#if 1
  if ( !vertexBufferRaw )
    return;

  if ( numBatches >= MAX_BATCHES )
    return;

  int point_count = _coords.size();

  if ( point_count < 2 )
    return;

  int vertex_count = ( point_count * 2 - 2) * 2 + 4;

  if ( numVertices + vertex_count > MAX_VERTEX_COUNT )
    return;

  int segment_count = point_count - 1;

  int vertexID = numVertices;

  const float coord0 = 0.123f;
  const float coord1 = 0.375f;
  const float coord2 = 0.625f;
  const float coord3 = 0.875f;

  float segU0 = coord1;
  float segDelta = (coord2 - coord1) / float( segment_count );
  float segU1 = coord1 + segDelta;

  for ( int i = 0; i < segment_count; ++i )
  {
    float perpX = float(_coords[i + 1].y - _coords[i].y);
    float perpY = float(_coords[i].x - _coords[i + 1].x);

    Normalize( perpX, perpY );

    float normX = -perpY;
    float normY = perpX;

    if ( i == 0 )
    {
      vertexBufferRaw[vertexID].x = _coords[i].x - normX * lineWidthHalf + perpX * lineWidthHalf;
      vertexBufferRaw[vertexID].y = _coords[i].y - normY * lineWidthHalf + perpY * lineWidthHalf;
      vertexBufferRaw[vertexID].xShift = perpX;
      vertexBufferRaw[vertexID].yShift = perpY;
      vertexBufferRaw[vertexID].color = lineColor;
      vertexBufferRaw[vertexID].aliasing = 0.f;
      vertexBufferRaw[vertexID].aliasingScale = lineWidthHalf;
      vertexBufferRaw[vertexID].distanceUV = CVec2( coord0, coord0 );
      vertexID++;

      vertexBufferRaw[vertexID].x = _coords[i].x - normX * lineWidthHalf - perpX * lineWidthHalf;
      vertexBufferRaw[vertexID].y = _coords[i].y - normY * lineWidthHalf - perpY * lineWidthHalf;
      vertexBufferRaw[vertexID].xShift = -perpX;
      vertexBufferRaw[vertexID].yShift = -perpY;
      vertexBufferRaw[vertexID].color = lineColor;
      vertexBufferRaw[vertexID].aliasing = 0.f;
      vertexBufferRaw[vertexID].aliasingScale = lineWidthHalf;
      vertexBufferRaw[vertexID].distanceUV = CVec2( coord2, coord0 );
      vertexID++;
    }

    vertexBufferRaw[vertexID].x = _coords[i].x + perpX * lineWidthHalf;
    vertexBufferRaw[vertexID].y = _coords[i].y + perpY * lineWidthHalf;
    vertexBufferRaw[vertexID].xShift = perpX;
    vertexBufferRaw[vertexID].yShift = perpY;
    vertexBufferRaw[vertexID].color = lineColor;
    vertexBufferRaw[vertexID].aliasing = 0.f;
    vertexBufferRaw[vertexID].aliasingScale = lineWidthHalf;
    vertexBufferRaw[vertexID].distanceUV = CVec2( coord0, segU0 );
    vertexID++;

    vertexBufferRaw[vertexID].x = _coords[i].x - perpX * lineWidthHalf;
    vertexBufferRaw[vertexID].y = _coords[i].y - perpY * lineWidthHalf;
    vertexBufferRaw[vertexID].xShift = -perpX;
    vertexBufferRaw[vertexID].yShift = -perpY;
    vertexBufferRaw[vertexID].color = lineColor;
    vertexBufferRaw[vertexID].aliasing = 1.f;
    vertexBufferRaw[vertexID].aliasingScale = lineWidthHalf;
    vertexBufferRaw[vertexID].distanceUV = CVec2( coord2, segU0 );
    vertexID++;

    vertexBufferRaw[vertexID].x = _coords[i+1].x + perpX * lineWidthHalf;
    vertexBufferRaw[vertexID].y = _coords[i+1].y + perpY * lineWidthHalf;
    vertexBufferRaw[vertexID].xShift = perpX;
    vertexBufferRaw[vertexID].yShift = perpY;
    vertexBufferRaw[vertexID].color = lineColor;
    vertexBufferRaw[vertexID].aliasing = 0.f;
    vertexBufferRaw[vertexID].aliasingScale = lineWidthHalf;
    vertexBufferRaw[vertexID].distanceUV = CVec2( coord0, segU1 );
    vertexID++;

    vertexBufferRaw[vertexID].x = _coords[i+1].x - perpX * lineWidthHalf;
    vertexBufferRaw[vertexID].y = _coords[i+1].y - perpY * lineWidthHalf;
    vertexBufferRaw[vertexID].xShift = -perpX;
    vertexBufferRaw[vertexID].yShift = -perpY;
    vertexBufferRaw[vertexID].color = lineColor;
    vertexBufferRaw[vertexID].aliasing = 1.f;
    vertexBufferRaw[vertexID].aliasingScale = lineWidthHalf;
    vertexBufferRaw[vertexID].distanceUV = CVec2( coord2, segU1 );
    vertexID++;

    segU0 += segDelta;
    segU1 += segDelta;

    if ( i == segment_count - 1 )
    {
      vertexBufferRaw[vertexID].x = _coords[i+1].x + normX * lineWidthHalf + perpX * lineWidthHalf;
      vertexBufferRaw[vertexID].y = _coords[i+1].y + normY * lineWidthHalf + perpY * lineWidthHalf;
      vertexBufferRaw[vertexID].xShift = perpX;
      vertexBufferRaw[vertexID].yShift = perpY;
      vertexBufferRaw[vertexID].color = lineColor;
      vertexBufferRaw[vertexID].aliasing = 0.f;
      vertexBufferRaw[vertexID].aliasingScale = lineWidthHalf;
      vertexBufferRaw[vertexID].distanceUV = CVec2( coord0, coord3 );
      vertexID++;

      vertexBufferRaw[vertexID].x = _coords[i+1].x + normX * lineWidthHalf - perpX * lineWidthHalf;
      vertexBufferRaw[vertexID].y = _coords[i+1].y + normY * lineWidthHalf - perpY * lineWidthHalf;
      vertexBufferRaw[vertexID].xShift = -perpX;
      vertexBufferRaw[vertexID].yShift = -perpY;
      vertexBufferRaw[vertexID].color = lineColor;
      vertexBufferRaw[vertexID].aliasing = 0.f;
      vertexBufferRaw[vertexID].aliasingScale = lineWidthHalf;
      vertexBufferRaw[vertexID].distanceUV = CVec2( coord2, coord3 );
      vertexID++;
    }
  }

  Batch& batch = batches[numBatches];

  batch.prepareFunc = &FlashRenderer::PrepareLine;

  batch.dip.primitiveType = RENDERPRIMITIVE_TRIANGLESTRIP;
  batch.dip.baseVertexIndex = numVertices;	
  batch.dip.startIndex = numVertices;	// $TODO error in Render
  batch.dip.numVertices = vertex_count;
  batch.dip.primitiveCount = vertex_count - 2;

  batch.isLine = true;

  numVertices += vertex_count;

  NextBatch();
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::SetFillStyleBitmap( IBitmapInfo* bi, const flash::SWF_MATRIX& m, EBitmapWrapMode::Enum wm, bool primary )
{
  if ( numBatches >= MAX_BATCHES )
    return;

  if ( primary )
  {
    batches[ numBatches ].diffuse1.matrixRow1 = CVec4( m.m_[0][0], m.m_[0][1], m.m_[0][2], 0.f );
    batches[ numBatches ].diffuse1.matrixRow2 = CVec4( m.m_[1][0], m.m_[1][1], m.m_[1][2], 0.f );
    batches[ numBatches ].diffuse1.useDiffuse = true;
    batches[ numBatches ].diffuse1.texture = static_cast<BitmapInfoD3D*>( bi )->m_texture;
    batches[ numBatches ].diffuse1.textureMode = wm;
    batches[ numBatches ].diffuse1.smoothing = true;
  }
  else
  {
    batches[ numBatches ].diffuse2.matrixRow1 = CVec4( m.m_[0][0], m.m_[0][1], m.m_[0][2], 0.f );
    batches[ numBatches ].diffuse2.matrixRow2 = CVec4( m.m_[1][0], m.m_[1][1], m.m_[1][2], 0.f );
    batches[ numBatches ].diffuse2.useDiffuse = true;
    batches[ numBatches ].diffuse2.texture = static_cast<BitmapInfoD3D*>( bi )->m_texture;
    batches[ numBatches ].diffuse2.textureMode = wm;
    batches[ numBatches ].diffuse2.smoothing = true;
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::SetLineWidth( float width )
{
  lineWidthHalf = width * 0.5f;// + PIXELS_TO_TWIPS( 1 ); 
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::SetLineColor( const flash::SWF_RGBA& color )
{
  lineColor.A = color.a;
  lineColor.R = color.r;
  lineColor.G = color.g;
  lineColor.B = color.b;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::DrawBitmap( IBitmapInfo* bi, float width, float height, int uniqueID, bool smoothing )
{
#if 1
  if ( !vertexBufferRaw )
    return;

  if ( numBatches >= MAX_BATCHES )
    return;

  if ( numVertices + 4 > MAX_VERTEX_COUNT )
    return;

  Batch& batch = batches[numBatches];

  batch.prepareFunc = &FlashRenderer::PrepareBitmap;

  float scaleU = bi->GetUV2().x - bi->GetUV1().x;
  float scaleV = bi->GetUV2().y - bi->GetUV1().y;

  batch.diffuse1.useDiffuse = true;
  batch.diffuse1.texture = static_cast<BitmapInfoD3D*>( bi )->m_texture;
  batch.diffuse1.matrixRow1 = CVec4( scaleU / width, 0.f, bi->GetUV1().x, 0.f );
  batch.diffuse1.matrixRow2 = CVec4( 0.f, scaleV / height, bi->GetUV1().y, 0.f );
  batch.diffuse1.scale9Grid = false;
  batch.diffuse1.textureMode = EBitmapWrapMode::CLAMP;
  batch.diffuse1.smoothing = smoothing;

  vertexBufferRaw[numVertices+0].x = 0.f; vertexBufferRaw[numVertices+0].textureUV.x = bi->GetUV1().x;
  vertexBufferRaw[numVertices+0].y = 0.f; vertexBufferRaw[numVertices+0].textureUV.y = bi->GetUV1().y;

  vertexBufferRaw[numVertices+1].x = 0.f; vertexBufferRaw[numVertices+1].textureUV.x = bi->GetUV1().x;
  vertexBufferRaw[numVertices+1].y = height; vertexBufferRaw[numVertices+1].textureUV.y = bi->GetUV2().y;

  vertexBufferRaw[numVertices+2].x = width; vertexBufferRaw[numVertices+2].textureUV.x = bi->GetUV2().x;
  vertexBufferRaw[numVertices+2].y = 0.f; vertexBufferRaw[numVertices+2].textureUV.y = bi->GetUV1().y;

  vertexBufferRaw[numVertices+3].x = width; vertexBufferRaw[numVertices+3].textureUV.x = bi->GetUV2().x;
  vertexBufferRaw[numVertices+3].y = height; vertexBufferRaw[numVertices+3].textureUV.y = bi->GetUV2().y;

  for ( int i = 0; i < 4; ++i )
  {
    vertexBufferRaw[numVertices + i].xShift = 0.f;
    vertexBufferRaw[numVertices + i].yShift = 0.f;
    vertexBufferRaw[numVertices + i].aliasing = 1.f;
    vertexBufferRaw[numVertices + i].aliasingScale = 1.f;
  }

  batch.dip.primitiveType = RENDERPRIMITIVE_TRIANGLESTRIP;
  batch.dip.baseVertexIndex = numVertices;	
  batch.dip.startIndex = numVertices;
  batch.dip.numVertices = 4;
  batch.dip.primitiveCount = 2;

  numVertices += 4;

  NextBatch();
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::AddRectangle( int firstVertex, float x1, float y1, float x2, float y2, float u1, float v1, float u2, float v2 )
{
  if ( !vertexBufferRaw )
    return;

  // first face
  vertexBufferRaw[firstVertex+0].x = x1; vertexBufferRaw[firstVertex+0].textureUV.x = u1;
  vertexBufferRaw[firstVertex+0].y = y1; vertexBufferRaw[firstVertex+0].textureUV.y = v1;
 
  vertexBufferRaw[firstVertex+1].x = x1; vertexBufferRaw[firstVertex+1].textureUV.x = u1;
  vertexBufferRaw[firstVertex+1].y = y2; vertexBufferRaw[firstVertex+1].textureUV.y = v2;

  vertexBufferRaw[firstVertex+2].x = x2; vertexBufferRaw[firstVertex+2].textureUV.x = u2;
  vertexBufferRaw[firstVertex+2].y = y1; vertexBufferRaw[firstVertex+2].textureUV.y = v1;

  // second face
  vertexBufferRaw[firstVertex+3].x = x1; vertexBufferRaw[firstVertex+3].textureUV.x = u1;
  vertexBufferRaw[firstVertex+3].y = y2; vertexBufferRaw[firstVertex+3].textureUV.y = v2;

  vertexBufferRaw[firstVertex+4].x = x2; vertexBufferRaw[firstVertex+4].textureUV.x = u2;
  vertexBufferRaw[firstVertex+4].y = y1; vertexBufferRaw[firstVertex+4].textureUV.y = v1;

  vertexBufferRaw[firstVertex+5].x = x2; vertexBufferRaw[firstVertex+5].textureUV.x = u2;
  vertexBufferRaw[firstVertex+5].y = y2; vertexBufferRaw[firstVertex+5].textureUV.y = v2;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::DrawBitmapScale9Grid( IBitmapInfo* bi, float width, float height, const flash::SWF_RECT& scale9Grid, float aspectX, float aspectY, int uniqueID, bool smoothing )
{
#if 1
  if ( !vertexBufferRaw )
    return;

  if ( numBatches >= MAX_BATCHES )
    return;

  if ( ( numVertices + 6 * 9 ) > MAX_VERTEX_COUNT )
    return;

  Batch& batch = batches[numBatches];

  batch.prepareFunc = &FlashRenderer::PrepareBitmap;

  batch.diffuse1.useDiffuse = true;
  batch.diffuse1.texture = static_cast<BitmapInfoD3D*>( bi )->m_texture;
  batch.diffuse1.scale9Grid = true;
  batch.diffuse1.textureMode = EBitmapWrapMode::CLAMP;
  batch.diffuse1.smoothing = smoothing;

  float u[4] = { 0.f, (scale9Grid.X1) / float( bi->GetWidth() ), (scale9Grid.X2 ) / float( bi->GetWidth() ), 1.f };
  float v[4] = { 0.f, (scale9Grid.Y1) / float( bi->GetHeight() ), (scale9Grid.Y2) / float( bi->GetHeight() ), 1.f };

  float x[4] = { 0.f, u[1] * width / aspectX, (1.f - (1.f - u[2] ) / aspectX) * width, width };
  float y[4] = { 0.f, v[1] * height / aspectY, (1.f - (1.f - v[2]) / aspectY) * height, height };

  float scaleU = bi->GetUV2().x - bi->GetUV1().x;
  float scaleV = bi->GetUV2().y - bi->GetUV1().y;

  for ( int  i = 0; i < 4; ++i )
  {
    u[i] = bi->GetUV1().x + u[i] * scaleU;
    v[i] = bi->GetUV1().y + v[i] * scaleV;
  }

  int nextID = 0;

  for ( int i = 0; i < 3; ++i )
  {
    for ( int j = 0; j < 3; ++j )
    {
      AddRectangle( numVertices + nextID, x[i], y[j], x[i+1], y[j+1], u[i], v[j], u[i+1], v[j+1] );
      nextID += 6;
    }
  }

  for ( int i = 0; i < 6 * 9; ++i )
  {
    vertexBufferRaw[numVertices + i].xShift = 0.f;
    vertexBufferRaw[numVertices + i].yShift = 0.f;
    vertexBufferRaw[numVertices + i].aliasing = 1.f;
    vertexBufferRaw[numVertices + i].aliasingScale = 1.f;
  }

  batch.dip.primitiveType = RENDERPRIMITIVE_TRIANGLELIST;
  batch.dip.baseVertexIndex = numVertices;	
  batch.dip.startIndex = numVertices;
  batch.dip.numVertices = 6 * 9;
  batch.dip.primitiveCount = 2 * 9;

  numVertices += 6 * 9;

  NextBatch();
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::BeginSubmitMask()
{
  if ( numBatches >= MAX_BATCHES )
    return;

  batches[ numBatches ].beginSubmitMask = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::EndSubmitMask()
{
  if ( numBatches >= MAX_BATCHES )
    return;

  batches[ numBatches ].endSubmitMask = true;
  batches[ numBatches ].visible = false;

  NextBatch();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::BeginUnSubmitMask()
{
  if ( numBatches >= MAX_BATCHES )
    return;

  batches[ numBatches ].beginUnsubmitMask = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::DisableMask()
{
  if ( numBatches >= MAX_BATCHES )
    return;

  batches[ numBatches ].disbaleMask = true;
  batches[ numBatches ].visible = false;

  NextBatch();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::BeginColorMatrix( const SHMatrix& _colorMatrix, const CVec4& _addColor )
{
  if ( numBatches >= MAX_BATCHES )
    return;

  batches[ numBatches ].colorMatrixBegin = true;
  batches[ numBatches ].colorMatrixEnd = false;
  batches[ numBatches ].colorMatrix = _colorMatrix;
  batches[ numBatches ].colorAdd = _addColor / 255.f;
  batches[ numBatches ].visible = false;

  NextBatch();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::ClearCaches()
{

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::DebugNextBatch()
{
  if ( numBatches >= MAX_BATCHES )
    return;

  batches[ numBatches ].debug = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::EndColorMatrix()
{
  if ( numBatches >= MAX_BATCHES )
    return;

  batches[ numBatches ].colorMatrixBegin = false;
  batches[ numBatches ].colorMatrixEnd = true;
  batches[ numBatches ].visible = false;

  NextBatch();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::RenderText( int _partID )
{
  if ( numBatches >= MAX_BATCHES )
    return;

  colorTransformCached = false;
  modelMatrixCached = false;

  batches[ numBatches ].isText = true;
  batches[ numBatches ].visible = false;
  batches[ numBatches ].hasBevel = nextTextWithBevel;
  batches[ numBatches ].bevelColor = bevelColor;
  batches[ numBatches ].textPartID = _partID;

  NextBatch();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::RenderTextBevel( bool withBevel, const flash::SWF_RGBA& _color, Texture* _fontTexture )
{
  nextTextWithBevel = withBevel;
  bevelColor = Render::HDRColor( _color.r / 255.f, _color.g / 255.f, _color.b / 255.f, _color.a / 255.f );
  fontTexture = _fontTexture;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::PrepareFontMaterial( BaseMaterial* _material, bool _drawBevel, const Render::HDRColor& _color )
{
  NDb::UIFontMaterial fontMaterialDesc;

  fontMaterialDesc.PrimaryColor = Render::HDRColor( 1.f, 1.f, 1.f, 1.f );
  fontMaterialDesc.SecondaryColor = _color;
  fontMaterialDesc.DrawContour = _drawBevel ? NDb::BOOLEANPIN_PRESENT : NDb::BOOLEANPIN_NONE;
  fontMaterialDesc.renderState.blendMode = NDb::BLENDMODE_LERPBYALPHA;

  fontMaterialDesc.DiffuseMap.samplerState.addressU = NDb::TEXTUREADDRESSTYPE_CLAMP;
  fontMaterialDesc.DiffuseMap.samplerState.addressV = NDb::TEXTUREADDRESSTYPE_CLAMP;
  fontMaterialDesc.DiffuseMap.samplerState.addressW = NDb::TEXTUREADDRESSTYPE_CLAMP;

  fontMaterialDesc.DiffuseMap.samplerState.magFilter = NDb::MAGFILTERTYPE_LINEAR;
  fontMaterialDesc.DiffuseMap.samplerState.minFilter = NDb::MINFILTERTYPE_LINEAR;
  fontMaterialDesc.DiffuseMap.samplerState.mipFilter = NDb::MIPFILTERTYPE_POINT;

  _material->FillMaterial( &fontMaterialDesc, 0, false );

  _material->SetUseDiffuse( NDb::BOOLEANPIN_PRESENT );

  if ( fontTexture )
    _material->GetDiffuseMap()->SetTexture( fontTexture );
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::BeginSubmitMaskApply()
{
  if ( maskLevel == 0 )
  {
    GetDevice()->SetRenderState(D3DRS_STENCILENABLE, TRUE);
    GetDevice()->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
    GetDevice()->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
    GetDevice()->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL );
    GetDevice()->SetRenderState(D3DRS_STENCILMASK, 0xFFFFFFFF);
    GetDevice()->SetRenderState(D3DRS_STENCILWRITEMASK, 0xFFFFFFFF);
//    GetDevice()->Clear(0, NULL, D3DCLEAR_STENCIL, 0, 1.0f, 0);
  }

  GetDevice()->SetRenderState(D3DRS_COLORWRITEENABLE, 0x00000000);
  GetDevice()->SetRenderState(D3DRS_STENCILREF, maskLevel);
  GetDevice()->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_INCR);

  maskLevel++;
}

void FlashRenderer::BeginUnSubmitMaskApply()
{
  GetDevice()->SetRenderState(D3DRS_COLORWRITEENABLE, 0x00000000);
  GetDevice()->SetRenderState(D3DRS_STENCILREF, maskLevel);
  GetDevice()->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_DECR);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::EndSubmitMaskApply()
{
  GetDevice()->SetRenderState(D3DRS_STENCILREF, maskLevel);
  GetDevice()->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
  GetDevice()->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0000000F);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::DisableMaskApply()
{
  NI_ASSERT( maskLevel > 0, " Mask level is wrong! " );

  --maskLevel;

  if ( maskLevel > 0 )
  {
    EndSubmitMaskApply();
  }

  if ( maskLevel == 0 )
  {
    GetDevice()->SetRenderState( D3DRS_STENCILENABLE, FALSE );
    GetDevice()->SetRenderState( D3DRS_COLORWRITEENABLE, 0x0000000F );
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FlashRenderer::NextBatch()
{
  numBatches++;

  if ( numBatches >= MAX_BATCHES )
    return;

  batches[ numBatches ].Clear();
}

} // namespace Render

NI_DEFINE_REFCOUNT( Render::IBitmapInfo );

#endif
