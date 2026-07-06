#include "stdafx.h"

#if defined(PW_LINUX_NULL_RENDER)

#include "smartrenderer.h"
#include "dipdescriptor.h"
#include "texture.h"
#include "vertexformatdescriptor.h"
#include "../System/matrix43.h"

#include <algorithm>
#include <cmath>

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
#include <GL/gl.h>
#endif

namespace
{
  unsigned int g_triangleCount = 0;
  unsigned int g_dipCount = 0;
  bool g_useViewport = false;
  bool g_instancingEnabled = false;
  int g_viewportX = 0;
  int g_viewportY = 0;
  int g_viewportWidth = 1;
  int g_viewportHeight = 1;
  IDirect3DSurface9* g_renderTargets[8] = {};
  IDirect3DSurface9* g_depthSurface = 0;
  IDirect3DSurface9* g_defaultRT0 = 0;
  IDirect3DSurface9* g_defaultRT1 = 0;
  IDirect3DSurface9* g_defaultDepth = 0;
  IDirect3DVertexBuffer9* g_boundVertexBuffer = 0;
  IDirect3DIndexBuffer9* g_boundIndexBuffer = 0;
  IDirect3DVertexDeclaration9* g_boundVertexDeclaration = 0;
  unsigned int g_boundVertexStride = 0;
  unsigned int g_boundVertexOffset = 0;
  nstl::vector<IDirect3DVertexDeclaration9*> g_vertexDeclarations;

  bool DeclarationMatches(const IDirect3DVertexDeclaration9* declaration, const Render::VertexFormatDescriptor& descr)
  {
    if (!declaration || declaration->elements.size() != descr.GetVertexElementsCount())
      return false;

    for (unsigned int i = 0; i < descr.GetVertexElementsCount(); ++i)
    {
      const Render::VertexElementDescriptor& source = descr.GetVertexElement(i);
      const NullVertexElementDescriptor& stored = declaration->elements[i];
      if (stored.stream != source.stream ||
          stored.offset != source.offset ||
          stored.type != static_cast<int>(source.type) ||
          stored.usage != static_cast<int>(source.usage) ||
          stored.usageIndex != source.usageIndex)
      {
        return false;
      }
    }

    return true;
  }

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
  bool g_openGLImmediateMeshDrawingEnabled = false;
  const Matrix43* g_openGLImmediateObjectMatrix = 0;
  const Matrix43* g_openGLImmediateSkeletalMatrices = 0;
  unsigned int g_openGLImmediateSkeletalMatrixCount = 0;
  float g_openGLPreviewCenterX = 0.0f;
  float g_openGLPreviewCenterY = 0.0f;
  float g_openGLPreviewMinZ = 0.0f;
  float g_openGLPreviewScale = 1.0f;

  float ClampFloat(float value, float minValue, float maxValue)
  {
    return value < minValue ? minValue : (value > maxValue ? maxValue : value);
  }

  unsigned char ScaleColorByte(unsigned char value, float scale)
  {
    const int scaled = static_cast<int>(static_cast<float>(value) * scale + 0.5f);
    return static_cast<unsigned char>(
      scaled < 0 ? 0 : (scaled > 255 ? 255 : scaled));
  }

  const NullVertexElementDescriptor* FindDeclarationElement(
    const IDirect3DVertexDeclaration9* declaration,
    Render::EVertexElementUsage usage,
    unsigned int usageIndex)
  {
    if (!declaration)
      return 0;

    for (unsigned int i = 0; i < declaration->elements.size(); ++i)
    {
      const NullVertexElementDescriptor& element = declaration->elements[i];
      if (element.stream == 0 &&
          element.usage == static_cast<int>(usage) &&
          element.usageIndex == usageIndex)
      {
        return &element;
      }
    }

    return 0;
  }

  bool ReadVertexFloat(const nstl::vector<unsigned char>& storage, size_t offset, float* value)
  {
    if (!value || offset + sizeof(float) > storage.size())
      return false;

    memcpy(value, &storage[offset], sizeof(float));
    return true;
  }

  bool ReadVertexFloat2(const nstl::vector<unsigned char>& storage, size_t offset, float* x, float* y)
  {
    return ReadVertexFloat(storage, offset, x) &&
      ReadVertexFloat(storage, offset + sizeof(float), y);
  }

  bool ReadVertexFloat3(const nstl::vector<unsigned char>& storage, size_t offset, CVec3* value)
  {
    if (!value)
      return false;

    return ReadVertexFloat(storage, offset, &value->x) &&
      ReadVertexFloat(storage, offset + sizeof(float), &value->y) &&
      ReadVertexFloat(storage, offset + sizeof(float) * 2, &value->z);
  }

  bool ReadVertexFloat4(const nstl::vector<unsigned char>& storage, size_t offset, float* values)
  {
    if (!values)
      return false;

    return ReadVertexFloat(storage, offset, &values[0]) &&
      ReadVertexFloat(storage, offset + sizeof(float), &values[1]) &&
      ReadVertexFloat(storage, offset + sizeof(float) * 2, &values[2]) &&
      ReadVertexFloat(storage, offset + sizeof(float) * 3, &values[3]);
  }

  bool ReadVertexBytes4(const nstl::vector<unsigned char>& storage, size_t offset, unsigned char* values)
  {
    if (!values || offset + 4 > storage.size())
      return false;

    values[0] = storage[offset + 0];
    values[1] = storage[offset + 1];
    values[2] = storage[offset + 2];
    values[3] = storage[offset + 3];
    return true;
  }

  bool ReadIndex(const IDirect3DIndexBuffer9* buffer, size_t indexNumber, unsigned int* value)
  {
    if (!buffer || !value)
      return false;

    // Null D3D buffers keep the original index format; OpenGL replay must match that stride.
    switch (buffer->format)
    {
      case D3DFMT_INDEX16:
      {
        const size_t offset = indexNumber * sizeof(unsigned short);
        if (offset + sizeof(unsigned short) > buffer->storage.size())
          return false;

        unsigned short indexValue = 0;
        memcpy(&indexValue, &buffer->storage[offset], sizeof(indexValue));
        *value = indexValue;
        return true;
      }

      case D3DFMT_INDEX32:
      {
        const size_t offset = indexNumber * sizeof(unsigned int);
        if (offset + sizeof(unsigned int) > buffer->storage.size())
          return false;

        memcpy(value, &buffer->storage[offset], sizeof(unsigned int));
        return true;
      }

      default:
        return false;
    }
  }

  void ApplyOpenGLPreviewTransform(const CVec3& source, float* x, float* y, float* z)
  {
    if (x)
      *x = (source.x - g_openGLPreviewCenterX) * g_openGLPreviewScale;
    if (y)
      *y = 0.04f + (source.z - g_openGLPreviewMinZ) * g_openGLPreviewScale;
    if (z)
      *z = (source.y - g_openGLPreviewCenterY) * g_openGLPreviewScale;
  }

  void EmitOpenGLImmediateMeshColor(const CVec3& normal, bool normalValid, bool textured)
  {
    float light = 1.0f;
    if (normalValid)
    {
      const float mappedX = normal.x;
      const float mappedY = normal.z;
      const float mappedZ = normal.y;
      const float dot = mappedX * -0.38f + mappedY * 0.72f + mappedZ * 0.58f;
      light = ClampFloat(0.54f + std::max(0.0f, dot) * 0.64f, 0.0f, 1.18f);
    }

    if (textured)
    {
      const unsigned char color = ScaleColorByte(255, light);
      glColor4ub(color, color, color, 238);
      return;
    }

    glColor4ub(
      ScaleColorByte(74, light),
      ScaleColorByte(138, light),
      ScaleColorByte(210, light),
      232);
  }

  bool BuildOpenGLImmediateMeshVertex(
    int effectiveVertexIndex,
    const NullVertexElementDescriptor* positionElement,
    const NullVertexElementDescriptor* normalElement,
    const NullVertexElementDescriptor* texCoordElement,
    const NullVertexElementDescriptor* blendWeightElement,
    const NullVertexElementDescriptor* blendIndicesElement,
    CVec3* position,
    CVec3* normal,
    bool* normalValid,
    float* u,
    float* v,
    bool* texCoordValid)
  {
    if (!g_boundVertexBuffer || !positionElement || !position || effectiveVertexIndex < 0)
      return false;

    const nstl::vector<unsigned char>& storage = g_boundVertexBuffer->storage;
    const size_t vertexBase =
      static_cast<size_t>(g_boundVertexOffset) +
      static_cast<size_t>(effectiveVertexIndex) * static_cast<size_t>(g_boundVertexStride);
    CVec3 rawPosition(0.0f, 0.0f, 0.0f);
    if (!ReadVertexFloat3(storage, vertexBase + positionElement->offset, &rawPosition))
      return false;

    CVec3 rawNormal(0.0f, 0.0f, 1.0f);
    bool hasNormal = false;
    if (normalElement && normalElement->type == static_cast<int>(Render::VERTEXELEMENTTYPE_FLOAT3))
    {
      hasNormal = ReadVertexFloat3(storage, vertexBase + normalElement->offset, &rawNormal);
    }

    bool hasSkinning = false;
    CVec3 skinnedPosition(0.0f, 0.0f, 0.0f);
    CVec3 skinnedNormal(0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;
    if (g_openGLImmediateSkeletalMatrices &&
        g_openGLImmediateSkeletalMatrixCount > 0 &&
        blendWeightElement &&
        blendIndicesElement &&
        blendWeightElement->type == static_cast<int>(Render::VERTEXELEMENTTYPE_FLOAT4) &&
        (blendIndicesElement->type == static_cast<int>(Render::VERTEXELEMENTTYPE_D3DCOLOR) ||
         blendIndicesElement->type == static_cast<int>(Render::VERTEXELEMENTTYPE_UBYTE4)))
    {
      float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      unsigned char indices[4] = {0, 0, 0, 0};
      if (ReadVertexFloat4(storage, vertexBase + blendWeightElement->offset, weights) &&
          ReadVertexBytes4(storage, vertexBase + blendIndicesElement->offset, indices))
      {
        for (unsigned int influence = 0; influence < 4; ++influence)
        {
          const float weight = weights[influence];
          const unsigned int matrixIndex = indices[influence];
          if (weight <= 0.0001f || matrixIndex >= g_openGLImmediateSkeletalMatrixCount)
            continue;

          const Matrix43& matrix = g_openGLImmediateSkeletalMatrices[matrixIndex];
          skinnedPosition += Transform(rawPosition, matrix) * weight;
          if (hasNormal)
            skinnedNormal += Rotate(rawNormal, matrix) * weight;
          totalWeight += weight;
          hasSkinning = true;
        }
      }
    }

    if (hasSkinning && totalWeight > 0.0001f)
    {
      if (fabs(totalWeight - 1.0f) > 0.02f)
      {
        skinnedPosition /= totalWeight;
        if (hasNormal)
          skinnedNormal /= totalWeight;
      }

      *position = skinnedPosition;
      if (hasNormal)
      {
        const float normalLengthSq = skinnedNormal.LengthSqr();
        if (normalLengthSq > 0.000001f)
        {
          const float invLength = 1.0f / sqrtf(normalLengthSq);
          *normal = skinnedNormal * invLength;
        }
        else
        {
          *normal = rawNormal;
        }
      }
    }
    else
    {
      *position = rawPosition;
      if (hasNormal)
        *normal = rawNormal;
    }

    if (!hasSkinning && g_openGLImmediateObjectMatrix)
    {
      *position = Transform(*position, *g_openGLImmediateObjectMatrix);
      if (hasNormal)
      {
        CVec3 transformedNormal = Rotate(*normal, *g_openGLImmediateObjectMatrix);
        const float normalLengthSq = transformedNormal.LengthSqr();
        if (normalLengthSq > 0.000001f)
        {
          const float invLength = 1.0f / sqrtf(normalLengthSq);
          *normal = transformedNormal * invLength;
        }
      }
    }

    if (normalValid)
      *normalValid = hasNormal;

    bool hasTexCoord = false;
    if (texCoordElement && texCoordElement->type == static_cast<int>(Render::VERTEXELEMENTTYPE_FLOAT2))
    {
      hasTexCoord = ReadVertexFloat2(storage, vertexBase + texCoordElement->offset, u, v);
    }
    if (texCoordValid)
      *texCoordValid = hasTexCoord;

    return true;
  }

  void DrawOpenGLImmediateIndexedPrimitive(const Render::DipDescriptor& descr)
  {
    if (!g_openGLImmediateMeshDrawingEnabled ||
        descr.primitiveType != Render::RENDERPRIMITIVE_TRIANGLELIST ||
        !g_boundVertexBuffer ||
        !g_boundIndexBuffer ||
        !g_boundVertexDeclaration ||
        g_boundVertexStride == 0 ||
        descr.primitiveCount == 0)
    {
      return;
    }

    const NullVertexElementDescriptor* positionElement =
      FindDeclarationElement(g_boundVertexDeclaration, Render::VERETEXELEMENTUSAGE_POSITION, 0);
    const NullVertexElementDescriptor* normalElement =
      FindDeclarationElement(g_boundVertexDeclaration, Render::VERETEXELEMENTUSAGE_NORMAL, 0);
    const NullVertexElementDescriptor* texCoordElement =
      FindDeclarationElement(g_boundVertexDeclaration, Render::VERETEXELEMENTUSAGE_TEXCOORD, 0);
    const NullVertexElementDescriptor* blendWeightElement =
      FindDeclarationElement(g_boundVertexDeclaration, Render::VERETEXELEMENTUSAGE_BLENDWEIGHT, 0);
    const NullVertexElementDescriptor* blendIndicesElement =
      FindDeclarationElement(g_boundVertexDeclaration, Render::VERETEXELEMENTUSAGE_BLENDINDICES, 0);
    if (!positionElement || positionElement->type != static_cast<int>(Render::VERTEXELEMENTTYPE_FLOAT3))
      return;

    const bool textureEnabled = glIsEnabled(GL_TEXTURE_2D) == GL_TRUE;
    glBegin(GL_TRIANGLES);
    for (unsigned int triangleIndex = 0; triangleIndex < descr.primitiveCount; ++triangleIndex)
    {
      for (unsigned int corner = 0; corner < 3; ++corner)
      {
        unsigned int indexValue = 0;
        if (!ReadIndex(
              g_boundIndexBuffer,
              static_cast<size_t>(descr.startIndex) +
                static_cast<size_t>(triangleIndex) * 3U +
                static_cast<size_t>(corner),
              &indexValue))
        {
          continue;
        }

        const int effectiveVertexIndex =
          descr.baseVertexIndex + static_cast<int>(indexValue);
        CVec3 position(0.0f, 0.0f, 0.0f);
        CVec3 normal(0.0f, 0.0f, 1.0f);
        bool normalValid = false;
        float u = 0.0f;
        float v = 0.0f;
        bool texCoordValid = false;
        if (!BuildOpenGLImmediateMeshVertex(
              effectiveVertexIndex,
              positionElement,
              normalElement,
              texCoordElement,
              blendWeightElement,
              blendIndicesElement,
              &position,
              &normal,
              &normalValid,
              &u,
              &v,
              &texCoordValid))
        {
          continue;
        }

        EmitOpenGLImmediateMeshColor(normal, normalValid, textureEnabled && texCoordValid);
        if (textureEnabled && texCoordValid)
          glTexCoord2f(u, v);

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        ApplyOpenGLPreviewTransform(position, &x, &y, &z);
        glVertex3f(x, y, z);
      }
    }
    glEnd();
  }
#endif
}

namespace Render
{
namespace SmartRenderer
{

void GetTriangleAndDipCount(unsigned int& triangles, unsigned int& dips)
{
  triangles = g_triangleCount;
  dips = g_dipCount;
}

void ResetTriangleAndDipCount()
{
  g_triangleCount = 0;
  g_dipCount = 0;
}

void OnFrameStart()
{
}

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
void SetOpenGLImmediateMeshDrawingEnabled(bool enabled)
{
  g_openGLImmediateMeshDrawingEnabled = enabled;
  if (!enabled)
  {
    g_openGLImmediateObjectMatrix = 0;
    g_openGLImmediateSkeletalMatrices = 0;
    g_openGLImmediateSkeletalMatrixCount = 0;
  }
}

void SetOpenGLImmediateMeshPreviewTransform(float centerX, float centerY, float minZ, float scale)
{
  g_openGLPreviewCenterX = centerX;
  g_openGLPreviewCenterY = centerY;
  g_openGLPreviewMinZ = minZ;
  g_openGLPreviewScale = scale > 0.0001f ? scale : 1.0f;
}

void SetOpenGLImmediateObjectMatrix(const Matrix43* matrix)
{
  g_openGLImmediateObjectMatrix = matrix;
}

void SetOpenGLImmediateSkeletalMatrices(const Matrix43* matrices, unsigned int matrixCount)
{
  g_openGLImmediateSkeletalMatrices = matrices;
  g_openGLImmediateSkeletalMatrixCount = matrixCount;
}
#endif

void AddRect(float l, float t, float r, float b, int R, int G, int B)
{
  (void)l;
  (void)t;
  (void)r;
  (void)b;
  (void)R;
  (void)G;
  (void)B;
}

void AddLine(float sx, float sy, float ex, float ey, int R, int G, int B, float width)
{
  (void)sx;
  (void)sy;
  (void)ex;
  (void)ey;
  (void)R;
  (void)G;
  (void)B;
  (void)width;
}

void RenderDeferred2D()
{
}

void Init()
{
}

void NullThePointers()
{
  g_boundVertexBuffer = 0;
  g_boundIndexBuffer = 0;
  g_boundVertexDeclaration = 0;
  g_boundVertexStride = 0;
  g_boundVertexOffset = 0;
  for (unsigned int i = 0; i < sizeof(g_renderTargets) / sizeof(g_renderTargets[0]); ++i)
    g_renderTargets[i] = 0;
  g_depthSurface = 0;
  g_defaultRT0 = 0;
  g_defaultRT1 = 0;
  g_defaultDepth = 0;
}

bool IsResourceBound(const IUnknown* const ptr)
{
  (void)ptr;
  return false;
}

const DXVertexDeclarationRef& GetVertexFormatDeclaration(const VertexFormatDescriptor& descr)
{
  static DXVertexDeclarationRef s_lastDeclaration = 0;

  for (unsigned int i = 0; i < g_vertexDeclarations.size(); ++i)
  {
    if (DeclarationMatches(g_vertexDeclarations[i], descr))
    {
      s_lastDeclaration = g_vertexDeclarations[i];
      return s_lastDeclaration;
    }
  }

  IDirect3DVertexDeclaration9* declaration = new IDirect3DVertexDeclaration9();
  declaration->elements.reserve(descr.GetVertexElementsCount());
  for (unsigned int i = 0; i < descr.GetVertexElementsCount(); ++i)
  {
    const VertexElementDescriptor& source = descr.GetVertexElement(i);
    NullVertexElementDescriptor stored;
    stored.stream = source.stream;
    stored.offset = source.offset;
    stored.type = static_cast<int>(source.type);
    stored.usage = static_cast<int>(source.usage);
    stored.usageIndex = source.usageIndex;
    declaration->elements.push_back(stored);
  }
  g_vertexDeclarations.push_back(declaration);
  s_lastDeclaration = declaration;
  return s_lastDeclaration;
}

void BindVertexBufferRaw(unsigned int streamNumber, IDirect3DVertexBuffer9* buffer, unsigned int stride, unsigned int offset)
{
  if (streamNumber == 0)
  {
    g_boundVertexBuffer = buffer;
    g_boundVertexStride = stride;
    g_boundVertexOffset = offset;
  }
}

void BindVertexBuffer(unsigned int streamNumber, IDirect3DVertexBuffer9* buffer, unsigned int stride, unsigned int offset)
{
  BindVertexBufferRaw(streamNumber, buffer, stride, offset);
}

void BindInstanceVB(unsigned int offset)
{
  (void)offset;
}

void UnBindVertexBufferRaw(UINT streamNumber)
{
  if (streamNumber == 0)
  {
    g_boundVertexBuffer = 0;
    g_boundVertexStride = 0;
    g_boundVertexOffset = 0;
  }
}

void UnBindVertexBuffer(UINT streamNumber)
{
  (void)streamNumber;
}

void EnableHardwareInstancing(UINT _numInstances, UINT _lastGeomStream, UINT _instanceStream)
{
  (void)_numInstances;
  (void)_lastGeomStream;
  (void)_instanceStream;
  g_instancingEnabled = true;
}

void DisableHardwareInstancing()
{
  g_instancingEnabled = false;
}

bool InstancingEnabled()
{
  return g_instancingEnabled;
}

void SetFVF(DWORD _fvf)
{
  (void)_fvf;
}

void DrawIndexedPrimitive(const DipDescriptor& _descr)
{
  if (_descr.primitiveType == RENDERPRIMITIVE_TRIANGLELIST)
    g_triangleCount += _descr.primitiveCount;
  ++g_dipCount;
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
  DrawOpenGLImmediateIndexedPrimitive(_descr);
#endif
}

void DrawIndexedPrimitiveUP(const DipDescriptor& _descr, const WORD* pIndexData, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
  (void)pIndexData;
  (void)pVertexStreamZeroData;
  (void)VertexStreamZeroStride;
  DrawIndexedPrimitive(_descr);
}

void DrawPrimitive(const DipDescriptor& descr)
{
  if (descr.primitiveType == RENDERPRIMITIVE_TRIANGLELIST)
    g_triangleCount += descr.primitiveCount;
  ++g_dipCount;
}

void DrawPrimitiveUP(const DipDescriptor& _descr, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
  (void)pVertexStreamZeroData;
  (void)VertexStreamZeroStride;
  DrawPrimitive(_descr);
}

void BindIndexBuffer(IDirect3DIndexBuffer9* buffer)
{
  g_boundIndexBuffer = buffer;
}

void BindVertexDeclarationRaw(IDirect3DVertexDeclaration9 *pDecl)
{
  g_boundVertexDeclaration = pDecl;
}

void BindVertexDeclaration(DXVertexDeclarationRef const &pDecl)
{
  BindVertexDeclarationRaw(Get(pDecl));
}

void BindVertexShader(IDirect3DVertexShader9 *shader)
{
  (void)shader;
}

void BindPixelShader(IDirect3DPixelShader9 *shader)
{
  (void)shader;
}

void BindTexture(unsigned int samplerIndex, const Texture* texture, bool bProtect)
{
  (void)bProtect;

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
  if (samplerIndex != 0)
    return;

  if (!texture)
  {
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    return;
  }

  const Texture2D* texture2D = dynamic_cast<const Texture2D*>(texture);
  const unsigned int openGLTexture = texture2D ? texture2D->GetOpenGLTexture() : 0;
  if (!openGLTexture)
    return;

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, openGLTexture);
#else
  (void)samplerIndex;
  (void)texture;
#endif
}

void UnBindTexture(unsigned int samplerIndex)
{
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
  if (samplerIndex == 0)
  {
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
  }
#else
  (void)samplerIndex;
#endif
}

void UnBindTexture(const Texture* texture)
{
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
  if (texture)
  {
    const Texture2D* texture2D = dynamic_cast<const Texture2D*>(texture);
    if (texture2D && texture2D->GetOpenGLTexture())
    {
      glBindTexture(GL_TEXTURE_2D, 0);
      glDisable(GL_TEXTURE_2D);
    }
  }
#else
  (void)texture;
#endif
}

void BindRenderTarget(const Texture2DRef& pColorTexture)
{
  (void)pColorTexture;
  g_renderTargets[0] = 0;
}

void BindRenderTarget(const Texture2DRef& pColorTexture, const RenderSurfaceRef& pDepthSurface)
{
  (void)pColorTexture;
  (void)pDepthSurface;
  g_renderTargets[0] = 0;
  g_depthSurface = 0;
}

void BindRenderTargetDefault()
{
  g_renderTargets[0] = g_defaultRT0;
  g_renderTargets[1] = g_defaultRT1;
  g_depthSurface = g_defaultDepth;
}

void SetDefaultRenderTarget(const DXSurfaceRef &pRT0, const DXSurfaceRef &pDepth)
{
  g_defaultRT0 = pRT0;
  g_defaultRT1 = 0;
  g_defaultDepth = pDepth;
}

void SetDefaultRenderTarget(const DXSurfaceRef &pRT0, const DXSurfaceRef &pRT1, const DXSurfaceRef &pDepth)
{
  g_defaultRT0 = pRT0;
  g_defaultRT1 = pRT1;
  g_defaultDepth = pDepth;
}

void BindRenderTargetColor(unsigned int renderTargetIndex, IDirect3DSurface9* pColorSurface)
{
  if (renderTargetIndex < sizeof(g_renderTargets) / sizeof(g_renderTargets[0]))
    g_renderTargets[renderTargetIndex] = pColorSurface;
}

void BindRenderTargetDepth(IDirect3DSurface9* pDepthStencilSurface)
{
  g_depthSurface = pDepthStencilSurface;
}

void SetMainViewport(int x, int y, int width, int height)
{
  g_viewportX = x;
  g_viewportY = y;
  g_viewportWidth = width;
  g_viewportHeight = height;
  g_useViewport = true;
}

void GetMainViewport(int& x, int& y, int& width, int& height)
{
  x = g_viewportX;
  y = g_viewportY;
  width = g_viewportWidth;
  height = g_viewportHeight;
}

void ResetMainViewport()
{
}

void FixViewport()
{
}

void SetUseMainViewport(bool _useViewport)
{
  g_useViewport = _useViewport;
}

bool UseMainViewport()
{
  return g_useViewport;
}

void Release()
{
  NullThePointers();
  for (unsigned int i = 0; i < g_vertexDeclarations.size(); ++i)
  {
    delete g_vertexDeclarations[i];
  }
  g_vertexDeclarations.clear();
}

IDirect3DSurface9* GetRenderTarget(unsigned int renderTargetIndex)
{
  if (renderTargetIndex < sizeof(g_renderTargets) / sizeof(g_renderTargets[0]))
    return g_renderTargets[renderTargetIndex];
  return 0;
}

IDirect3DSurface9* GetRenderTargetDepth()
{
  return g_depthSurface;
}

int GetRenderTargetWidth()
{
  return g_viewportWidth;
}

int GetRenderTargetHeight()
{
  return g_viewportHeight;
}

void DumpScreenshot(const nstl::string& filename, bool keepAlpha)
{
  (void)filename;
  (void)keepAlpha;
}

int GetScreenshotCount()
{
  return 0;
}

byte* DumpScreenshotToMemory(int width, int height)
{
  (void)width;
  (void)height;
  return 0;
}

} // namespace SmartRenderer
} // namespace Render

#else

#include "smartrenderer.h"

#include "shadercompiler.h"
#include "renderresourcemanager.h"
#include "uirenderer.h"
#include "../System/staticarray.h"
#include "texture.h"
#include "renderflagsconverter.h"
#include "OcclusionQueries.h"
#include "GlobalMasks.h"
#include "DXWarnSignal.h"
#include <vector>

static DXWarnSignal s_DXWarnLevel;

static bool nullrender = false;
REGISTER_VAR( "nodip", nullrender, STORAGE_NONE );

#ifdef _SUPRESS_HWI
static bool s_supressDrawCall = false;
static bool s_supressHWI = false;
REGISTER_DEV_VAR( "supressHWI", s_supressHWI, STORAGE_NONE );
#endif // _SUPRESS_HWI

#ifndef _SHIPPING

namespace
{
  NDebug::DebugVar<int> g_numVSu( "numVSUniq", "Statistics" );
  NDebug::DebugVar<int> g_numPSu( "numPSUniq", "Statistics" );
  NDebug::DebugVar<int> g_numVSt( "numVSTotal", "Statistics" );
  NDebug::DebugVar<int> g_numPSt( "numPSTotal", "Statistics" );
  NDebug::DebugVar<int> g_numVS( "numVS", "Statistics", true );
  NDebug::DebugVar<int> g_numPS( "numPS", "Statistics", true );
  hash_set<void*>  psMap, vsMap, psMapTotal, vsMapTotal;
}

#endif // _SHIPPING

static unsigned int  triangleCount = 0;
static unsigned int  dipCount = 0;

namespace Render
{
namespace SmartRenderer
{

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  struct InitIdxVB : public IntrusivePtrDeleter
  {
    typedef DXVertexBufferRef Type;

    void Init(Type& _result)
    {
      // Create & fill second vertex buffer
      _result = CreateVB(MAX_INSTANCES_COUNT * sizeof(int), RENDER_POOL_MANAGED);
      NI_VERIFY( _result, "Couldn't create instance idx VB", return );

      if( int * const pBuff = LockVB<int>(Get(_result), 0) )
      {
        for(size_t i = 0; i < MAX_INSTANCES_COUNT; i++)
          pBuff[i] = i;

        _result->Unlock();
      }
      else
        _result.Attach(0);
    }
  };

  static ManagedResource<InitIdxVB> s_pInstanceIndexVB;

///////////////////////////////////// 
static nstl::hash_map< VertexFormatDescriptor, DXVertexDeclarationRef > VertexDeclaraionsCash;
typedef nstl::hash_map< VertexFormatDescriptor, DXVertexDeclarationRef >::iterator VertexDeclarationsCashIterator;

enum StateValueType
{
  SVT_REQUESTED = 0,
  SVT_ACTIVE,
  SVT_TOTAL_NUM // this value MUST be last!
};

static const unsigned int MAX_STREAMS_COUNT = 16;
static StaticArray<IDirect3DVertexBuffer9*,MAX_STREAMS_COUNT> lastVertexBuffersPtrs;
static StaticArray<unsigned int,MAX_STREAMS_COUNT> lastVertexBuffersOffsets;
static StaticArray<unsigned int,MAX_STREAMS_COUNT> lastStreamSourceFreqs[SVT_TOTAL_NUM];
static IDirect3DIndexBuffer9* pLastIndexBuffer = 0; 

struct SamplerInfo
{
	const Texture *pTex;
	bool     bProtected;
};
static StaticArray<SamplerInfo, Sampler::MAX_VS_SAMPLER_INDEX+1> lastTexturesPtrs;

static IDirect3DVertexDeclaration9* pLastVertexDeclaration = 0;

static IDirect3DPixelShader9* pLastPixelShader = 0;
static IDirect3DVertexShader9* pLastVertexShader = 0;

static const unsigned int MAX_COLOR_RT = 8;
static StaticArray<IDirect3DSurface9*,MAX_COLOR_RT> curColorSurface;
static DWORD curColorSurfaceWidth = 1, curColorSurfaceHeight = 1;
static DWORD curDepthSurfaceWidth = 1, curDepthSurfaceHeight = 1;
static IDirect3DSurface9* curDepthSurface( 0 );

static IDirect3DSurface9* pDefaultRT0( 0 );
static IDirect3DSurface9* pDefaultRT1( 0 );
static IDirect3DSurface9* pDefaultDepth( 0 );

static D3DVIEWPORT9 viewport = { 0 };

namespace {
  bool useViewport = false;
  bool instancingEnabled = false;


  class DeclMap : public DeviceDeleteHandler
  {
    typedef nstl::hash_map<void*, DXVertexDeclarationRef> Cache;

    Cache cache;

  public:
    virtual void OnDeviceDelete()
    {
      cache.clear();
    }

    IDirect3DVertexDeclaration9* Get(IDirect3DVertexDeclaration9 *_pDecl)
    {
      Cache::iterator it = cache.find(_pDecl);
      if(cache.end() == it)
      {
        D3DVERTEXELEMENT9 decl[MAXD3DDECLLENGTH];
        UINT numElements;
        HRESULT hr = _pDecl->GetDeclaration(decl, &numElements); hr;

        for(UINT n = 0; n < numElements-1; ++n)
          if(decl[n].Stream)
            ++decl[n].Stream;

        static const D3DVERTEXELEMENT9 instanceElement = {1, 0, D3DDECLTYPE_SHORT2, 0, D3DDECLUSAGE_BLENDINDICES, 0};
        static const D3DVERTEXELEMENT9 declEnd = D3DDECL_END();

        decl[numElements-1] = instanceElement;
        decl[numElements] = declEnd;

        IDirect3DVertexDeclaration9 *pNewDecl = 0;
        hr = GetDevice()->CreateVertexDeclaration(decl, &pNewDecl);
        cache[_pDecl] = pNewDecl;
        return pNewDecl;
      }
      return ::Get(it->second);
    }
  };

  static DeclMap s_instancedVDecl;
}


//
// This part of smart renderer handles batching and deferred rendering execution
//
struct Vertex2D
{
  Vertex2D(float x, float y, float z, DWORD color)
  {
    this->x = x;
    this->y = y;
    this->z = z;
    this->w = 1.0f;
    this->color = color;
  }
  float x, y, z, w;
  DWORD color;
};

std::vector<Vertex2D> g_deferredVerts;
void AddRect(float l, float t, float r, float b, int R, int G, int B)
{
  DWORD color = D3DCOLOR_XRGB(R, G, B);
  std::vector<Vertex2D>& verts = g_deferredVerts;
  verts.push_back(Vertex2D(l, t, 0.0f, color));
  verts.push_back(Vertex2D(r, t, 0.0f, color));
  verts.push_back(Vertex2D(l, b, 0.0f, color));
  verts.push_back(Vertex2D(l, b, 0.0f, color));
  verts.push_back(Vertex2D(r, t, 0.0f, color));
  verts.push_back(Vertex2D(r, b, 0.0f, color));
}

void AddLine(float sx, float sy, float ex, float ey, int R, int G, int B, float width /* 1.0f */)
{
  DWORD color = D3DCOLOR_XRGB(R, G, B);
  std::vector<Vertex2D>& verts = g_deferredVerts;
  verts.push_back(Vertex2D(sx, sy, 0.0f, color));
  verts.push_back(Vertex2D(sx + width, sy, 0.0f, color));
  verts.push_back(Vertex2D(ex, ey, 0.0f, color));
  verts.push_back(Vertex2D(ex, ey, 0.0f, color));
  verts.push_back(Vertex2D(sx + width, sy, 0.0f, color));
  verts.push_back(Vertex2D(ex + width, ey, 0.0f, color));
}

void RenderDeferred2D()
{
  if(g_deferredVerts.size() > 2)
  {
    // Use fixed-function transformation to avoid custom extra shader here.
    // Draw call uses currently bound font shader which is good enough, thus
    // this function might not work if shader is not the font shader anymore.

    std::vector<Vertex2D>& verts = g_deferredVerts;   

    GetDevice()->SetVertexShader(NULL);
    GetDevice()->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    GetDevice()->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    GetDevice()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, verts.size() / 3, &verts[0], sizeof(Vertex2D));

    verts.clear();
  }
}



//////////////////////////////////////////////////////////////////////////
void GetTriangleAndDipCount(unsigned int& triangles, unsigned int& dips)
{
  triangles = triangleCount;
  dips = dipCount;
}

//////////////////////////////////////////////////////////////////////////
void ResetTriangleAndDipCount()
{
  triangleCount = 0;
  dipCount = 0;
}

// Lazy states application system
static void ApplyStates()
{
  IDirect3DDevice9* const device = GetDevice();
  StaticArray<unsigned int,MAX_STREAMS_COUNT> &ssfActive = lastStreamSourceFreqs[SVT_ACTIVE],
                                              &ssfRequested = lastStreamSourceFreqs[SVT_REQUESTED];
#ifndef _SUPRESS_HWI
  //if(ssfActive[1] != ssfRequested[1] && ssfRequested[1] == 1)
  //  UnBindVertexBufferRaw(1);

  for(UINT n = 0; n < MAX_STREAMS_COUNT; ++n)
    if( ssfActive[n] != ssfRequested[n] )
      device->SetStreamSourceFreq(n, ssfActive[n] = ssfRequested[n]);
#else
  bool isInstanced = false;
  for(UINT n = 0; n < MAX_STREAMS_COUNT; ++n)
    if( ssfActive[n] != ssfRequested[n] )
      if( s_supressHWI )
        isInstanced |= ssfRequested[n] != 1;
      else
        device->SetStreamSourceFreq(n, ssfActive[n] = ssfRequested[n]);

  s_supressDrawCall = s_supressHWI && isInstanced;
#endif // _SUPRESS_HWI
}

//////////////////////////////////////////////////////////////////////////
static void NullLastPointers()
{
  pLastIndexBuffer = 0;
  for (unsigned int i = 0; i < MAX_STREAMS_COUNT; ++i) 
  {
    lastVertexBuffersPtrs[i] = 0;
    lastVertexBuffersOffsets[i] = 0;
  }
  for (unsigned int i = 0; i < Sampler::MAX_VS_SAMPLER_INDEX; ++i) 
  {
    lastTexturesPtrs[i].pTex = 0;
    lastTexturesPtrs[i].bProtected = false;
  }

  pLastPixelShader = 0;
  pLastVertexShader = 0;
  pLastVertexDeclaration = 0;

  curDepthSurface = 0;
  for (unsigned int i = 0; i < MAX_COLOR_RT; ++i) 
    curColorSurface[i] = 0;

  DisableHardwareInstancing();
}

//////////////////////////////////////////////////////////////////////////
void NullThePointers()
{
// Sorry for profiling code [11/17/2009 smirnov]
//  _DebugSamplerBindReset();

  NullLastPointers();

	pDefaultRT0 = 0;
	pDefaultRT1 = 0;
	pDefaultDepth = 0;
}


//////////////////////////////////////////////////////////////////////////
void OnFrameStart()
{
#ifndef _SHIPPING
  g_numPSu.SetValue( psMap.size() );
  g_numVSu.SetValue( vsMap.size() );
  g_numPSt.SetValue( psMapTotal.size() );
  g_numVSt.SetValue( vsMapTotal.size() );

  psMap.clear(); vsMap.clear();
#endif // _SHIPPING
  NullLastPointers();
  OcclusionQueriesBank::OnFrameStart();
}

bool IsResourceBound(const IUnknown* const ptr)
{
  if((ptr == pLastIndexBuffer)||(ptr == pLastVertexShader)||(ptr == pLastPixelShader)
     ||(ptr == pLastVertexDeclaration)||(ptr == Get(curDepthSurface) ))
    return true;

	for(UINT n = 0; n < MAX_COLOR_RT; ++n) 
		if( ptr == Get(curColorSurface[n]) )
      return true;

	for(UINT n = 0; n < Sampler::MAX_VS_SAMPLER_INDEX; ++n)
		if( const Texture *pTex = lastTexturesPtrs[n].pTex )
		  if( ptr == Get(pTex->GetDXTexture()) )
        return true;

  for(UINT n = 0; n < MAX_STREAMS_COUNT; ++n)
	  if( ptr == lastVertexBuffersPtrs[n] )
      return true;

    return false;
}


void Init()
{
	NullThePointers();

	RenderResourceManager::Init();
}

///
void BindVertexBufferRaw(unsigned int streamNumber, IDirect3DVertexBuffer9* buffer, unsigned int stride, unsigned int offset)
{
	NI_VERIFY(streamNumber < MAX_STREAMS_COUNT, "Invalid vertex stream number!", return; );
	NI_VERIFY(buffer != 0, "Invalid vertex buffer!", return; );

	if (lastVertexBuffersPtrs[streamNumber] == buffer && lastVertexBuffersOffsets[streamNumber] == offset) 
		return;

	lastVertexBuffersPtrs[streamNumber] = buffer;
  lastVertexBuffersOffsets[streamNumber] = offset;
	HRESULT hr = GetDevice()->SetStreamSource(streamNumber, buffer, offset, stride);
  NI_DX_THROW(hr, "BindVertexBufferRaw");
}

///
void BindVertexBuffer(unsigned int streamNumber, IDirect3DVertexBuffer9* buffer, unsigned int stride, unsigned int offset)
{
  if(instancingEnabled && streamNumber)
    ++streamNumber;
  BindVertexBufferRaw(streamNumber, buffer, stride, offset);
}

void BindInstanceVB(unsigned int offset)
{
  BindVertexBufferRaw(1, s_pInstanceIndexVB.Get(), 4, offset);
}

///
void UnBindVertexBufferRaw(unsigned int streamNumber)
{
  NI_VERIFY(streamNumber < MAX_STREAMS_COUNT, "Invalid vertex stream number!", return; );

  if(!lastVertexBuffersPtrs[streamNumber]) 
    return;

  lastVertexBuffersPtrs[streamNumber] = 0;
  lastVertexBuffersOffsets[streamNumber] = 0;
  GetDevice()->SetStreamSource(streamNumber, 0, 0, 0);
}

///
void UnBindVertexBuffer(unsigned int streamNumber)
{
  if(instancingEnabled && streamNumber)
    ++streamNumber;
  UnBindVertexBufferRaw(streamNumber);
}

///
void EnableHardwareInstancing(UINT _numInstances, UINT _lastGeomStream, UINT _instanceStream)
{
  NI_VERIFY(_lastGeomStream < MAX_STREAMS_COUNT, "Invalid geometry streams number!", return; );
  NI_VERIFY(_numInstances < D3DSTREAMSOURCE_INDEXEDDATA, "Invalid instances number!", return; );

  if(_instanceStream == UINT_MAX)
    _instanceStream = _lastGeomStream + 1;

  NI_VERIFY(_instanceStream < MAX_STREAMS_COUNT, "Invalid instance stream number!", return; );

  instancingEnabled = true;

  for(UINT stream = 0; stream <= _lastGeomStream; ++stream)
  {
    lastStreamSourceFreqs[SVT_REQUESTED][stream] = UINT(D3DSTREAMSOURCE_INDEXEDDATA)|_numInstances;
  }
  lastStreamSourceFreqs[SVT_REQUESTED][_instanceStream] = UINT(D3DSTREAMSOURCE_INSTANCEDATA)|1;
}

///
void DisableHardwareInstancing()
{
  instancingEnabled = false;
  for(UINT stream = 0; stream < MAX_STREAMS_COUNT; ++stream)
    lastStreamSourceFreqs[SVT_REQUESTED][stream] = 1;
}

///                                                                     
bool InstancingEnabled() { return instancingEnabled; }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void SetFVF(DWORD _fvf)
{
  GetDevice()->SetFVF(_fvf);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef _DEBUG
  //#define _DEBUG_VIEWPORT
#endif // _DEBUG

void DrawIndexedPrimitive(const DipDescriptor& _descr)
{
  if(nullrender)
    return;

  ApplyStates();

#ifdef _SUPRESS_HWI
  if(s_supressDrawCall)
    return;
#endif // _SUPRESS_HWI

#ifdef _DEBUG_VIEWPORT
  D3DVIEWPORT9 vp;
  GetDevice()->GetViewport(&vp);
#endif // _DEBUG_VIEWPORT

  NI_VERIFY(_descr.numVertices, "null dip", return; );
  NI_VERIFY(_descr.primitiveCount, "null dip", return;);
  HRESULT hr = GetDevice()->DrawIndexedPrimitive( ConvertPrimitiveType(_descr.primitiveType), 
    _descr.baseVertexIndex, _descr.minIndex, _descr.numVertices, _descr.startIndex, _descr.primitiveCount );

  //NI_ASSERT(hr == D3D_OK, NStr::StrFmt("DrawIndexedPrimitive() failed! code: 0x%08X, error: %s description: %s", hr, DXGetErrorStringA(hr), DXGetErrorDescriptionA(hr)) );
  if(s_DXWarnLevel)
    NI_DX_WARN(hr, "DrawIndexedPrimitive");
  triangleCount += _descr.primitiveCount;
  ++dipCount;

  /* {
    IDirect3DDevice9* const device = GetDevice();
    DWORD val;
    hr = device->GetRenderState(D3DRS_STENCILENABLE, &val);
    hr = device->GetRenderState(D3DRS_STENCILFAIL, &val);
    hr = device->GetRenderState(D3DRS_STENCILZFAIL, &val);
    hr = device->GetRenderState(D3DRS_STENCILPASS, &val);
    hr = device->GetRenderState(D3DRS_STENCILFUNC, &val);
    hr = device->GetRenderState(D3DRS_STENCILREF, &val);
    hr = device->GetRenderState(D3DRS_STENCILMASK, &val);
    hr = device->GetRenderState(D3DRS_STENCILWRITEMASK, &val);

    hr = device->GetRenderState(D3DRS_ZFUNC, &val);
    hr = hr;
  } */

  return;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DrawIndexedPrimitiveUP(const DipDescriptor& _descr, const WORD* pIndexData,
                            const void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
  if(nullrender)
    return;

  ApplyStates();

#ifdef _SUPRESS_HWI
  if(s_supressDrawCall)
    return;
#endif // _SUPRESS_HWI

  NI_VERIFY(_descr.numVertices, "null dip", return; );
  NI_VERIFY(_descr.primitiveCount, "null dip", return;);
  HRESULT hr = GetDevice()->DrawIndexedPrimitiveUP(
                  static_cast<D3DPRIMITIVETYPE>( ConvertPrimitiveType(_descr.primitiveType) ), 
                  _descr.minIndex, _descr.numVertices, _descr.primitiveCount,
                  pIndexData, D3DFMT_INDEX16, pVertexStreamZeroData, VertexStreamZeroStride);

  if(s_DXWarnLevel)
    NI_DX_WARN(hr, "DrawIndexedPrimitiveUP");
  triangleCount += _descr.primitiveCount;
  ++dipCount;

  return;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DrawPrimitive(const DipDescriptor& _descr)
{
  if(nullrender)
    return;

  ApplyStates();

#ifdef _SUPRESS_HWI
  if(s_supressDrawCall)
    return;
#endif // _SUPRESS_HWI

#ifdef _DEBUG_VIEWPORT
  D3DVIEWPORT9 vp;
  GetDevice()->GetViewport(&vp);
#endif // _DEBUG_VIEWPORT

  NI_VERIFY(_descr.primitiveCount, "null dip", return;);
  HRESULT hr = GetDevice()->DrawPrimitive(
    static_cast<D3DPRIMITIVETYPE>( ConvertPrimitiveType(_descr.primitiveType) ), 
    _descr.startIndex, _descr.primitiveCount );

  if(s_DXWarnLevel)
    NI_DX_WARN(hr, "DrawPrimitive");
  triangleCount += _descr.primitiveCount;
  ++dipCount;

  return;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DrawPrimitiveUP(const DipDescriptor& _descr, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
  if(nullrender)
    return;

  ApplyStates();

#ifdef _SUPRESS_HWI
  if(s_supressDrawCall)
    return;
#endif // _SUPRESS_HWI

  NI_VERIFY(_descr.primitiveCount, "null dip", return;);
  HRESULT hr = GetDevice()->DrawPrimitiveUP(static_cast<D3DPRIMITIVETYPE>(ConvertPrimitiveType(_descr.primitiveType)),
                                            _descr.primitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
  if(s_DXWarnLevel)
    NI_DX_WARN(hr, "DrawPrimitiveUP");
  triangleCount += _descr.primitiveCount;
  ++dipCount;

  return;
}

///
void BindIndexBuffer(IDirect3DIndexBuffer9* buffer)
{
	NI_VERIFY(buffer != 0, "Invalid index buffer!", return; );
	if (pLastIndexBuffer == buffer) 
		return;

	pLastIndexBuffer = buffer;
	HRESULT hr = GetDevice()->SetIndices(pLastIndexBuffer);
  if(s_DXWarnLevel > 1)
    NI_DX_WARN(hr, "BindIndexBuffer");
}

///
const DXVertexDeclarationRef & GetVertexFormatDeclaration(const VertexFormatDescriptor& descr)
{
	VertexDeclarationsCashIterator it = VertexDeclaraionsCash.find(descr);
	if ( it != VertexDeclaraionsCash.end() )
		return it->second;

	VertexDeclaraionsCash[descr] = GetRenderer()->CreateVertexFormatDeclaration(descr);
	return VertexDeclaraionsCash[descr];
}

///
void BindVertexDeclarationRaw(IDirect3DVertexDeclaration9 *pDecl)
{
	if(pLastVertexDeclaration == pDecl) 
		return;

  HRESULT hr = GetDevice()->SetVertexDeclaration(pDecl);
  if(s_DXWarnLevel > 1)
    NI_DX_WARN(hr, "BindVertexDeclarationRaw");
	pLastVertexDeclaration = pDecl;
}

///
void BindVertexDeclaration(DXVertexDeclarationRef const &pDecl)
{
	IDirect3DVertexDeclaration9 *decl = Get(pDecl);
  if(instancingEnabled)
    decl = s_instancedVDecl.Get(decl);

	BindVertexDeclarationRaw(decl);
}


///
void Release()
{
	VertexDeclaraionsCash.clear();
	RenderResourceManager::Release();

	NullThePointers();
}

///
void BindTexture(unsigned int samplerIndex, const Texture* pTexture, bool bProtect)
{
	if (!pTexture) // @BVS@MAT
		return;
	NI_ASSERT(!lastTexturesPtrs[samplerIndex].bProtected || bProtect, "Polluting protected sampler");
	if ( lastTexturesPtrs[samplerIndex].pTex == pTexture) 
		return;
	
	lastTexturesPtrs[samplerIndex].pTex       = pTexture;
#ifdef _DEBUG
	lastTexturesPtrs[samplerIndex].bProtected = bProtect;
#endif
  HRESULT hr = GetDevice()->SetTexture( samplerIndex, Get(pTexture->GetDXTexture()) );
  if(s_DXWarnLevel > 1)
    NI_DX_WARN(hr, "BindTexture");
}

///
void UnBindTexture(unsigned int samplerIndex)
{
	if( !lastTexturesPtrs[samplerIndex].pTex ) 
		return;
	
	lastTexturesPtrs[samplerIndex].pTex = 0;
	GetDevice()->SetTexture(samplerIndex, 0);
}

///
void UnBindTexture(const Texture* texture)
{
  for(uint n = 0; n <= Sampler::MAX_PS_SAMPLER_INDEX; ++n)
    if(lastTexturesPtrs[n].pTex == texture) {
      UnBindTexture(n);
      break;
    }
}

///
void BindVertexShader(IDirect3DVertexShader9 *shader)
{
	NI_ASSERT(shader, "pointer to vertex shader is not valid!");
	if(pLastVertexShader == shader)
		return;

#ifndef _SHIPPING
  vsMap.insert(shader);
  vsMapTotal.insert(shader);
  g_numVS.AddValue( 1 );
#endif // _SHIPPING

	pLastVertexShader = shader;
	GetDevice()->SetVertexShader(shader);
}

/// 
void BindPixelShader(IDirect3DPixelShader9 *shader)
{
	NI_ASSERT(shader, "pointer to pixel shader is not valid!");
	if(pLastPixelShader == shader)
    return;

#ifndef _SHIPPING
  psMap.insert(shader);
  psMapTotal.insert(shader);
  g_numPS.AddValue( 1 );
#endif // _SHIPPING

	pLastPixelShader = shader;
	GetDevice()->SetPixelShader(shader);
}


void BindRenderTargetColor( unsigned int renderTargetIndex, IDirect3DSurface9* pColorSurface )
{
  if ( pColorSurface != curColorSurface[renderTargetIndex] )
	{
    HRESULT hr = GetDevice()->SetRenderTarget( renderTargetIndex, pColorSurface );
    curColorSurface[renderTargetIndex] = pColorSurface;
    if(s_DXWarnLevel > 1)
      NI_DX_WARN(hr, "BindRenderTargetColor");

    if (0 == renderTargetIndex)
    {
      D3DSURFACE_DESC rtDesc;
      pColorSurface->GetDesc(&rtDesc);
      curColorSurfaceWidth = rtDesc.Width;
      curColorSurfaceHeight = rtDesc.Height;
    }
	}
}

void BindRenderTargetDepth( IDirect3DSurface9* pDepthStencilSurface )
{
	if ( pDepthStencilSurface != curDepthSurface )
	{
    HRESULT hr = GetDevice()->SetDepthStencilSurface(pDepthStencilSurface);
		curDepthSurface = pDepthStencilSurface;
    if(s_DXWarnLevel > 1)
      NI_DX_WARN(hr, "BindRenderTargetDepth");

    if(curDepthSurface) {
      D3DSURFACE_DESC desc;
      curDepthSurface->GetDesc(&desc);
      curDepthSurfaceWidth = desc.Width;
      curDepthSurfaceHeight = desc.Height;
    }
	}
}

void SetDefaultRenderTarget(const DXSurfaceRef &pRT0, const DXSurfaceRef &pDepth)
{
	pDefaultRT0 = ::Get(pRT0);
	pDefaultRT1 = 0;
	pDefaultDepth = ::Get(pDepth);
}

void SetDefaultRenderTarget(const DXSurfaceRef &pRT0, const DXSurfaceRef &pRT1, const DXSurfaceRef &pDepth)
{
	pDefaultRT0 = ::Get(pRT0);
	pDefaultRT1 = ::Get(pRT1);
	pDefaultDepth = ::Get(pDepth);
}

void BindRenderTargetDefault()
{
	BindRenderTargetColor(0, pDefaultRT0);
	BindRenderTargetColor(1, pDefaultRT1);
	BindRenderTargetDepth(pDefaultDepth);

  if(useViewport) {
    HRESULT hr = GetDevice()->SetViewport( &viewport ); // smirnov [2009/7/15]: why set it here? why only here?
    if(s_DXWarnLevel > 1)
      NI_DX_WARN(hr, "BindRenderTargetDefault");
  }
}

void BindRenderTarget(const Texture2DRef& pColorTexture)
{
  NI_VERIFY(pColorTexture, "Texture Ref Should be valid!", return;);
  const DXSurfaceRef &pDXColorSurface = pColorTexture->GetSurface(0);
  BindRenderTargetColor(0, pDXColorSurface);
	BindRenderTargetColor(1, 0);
	BindRenderTargetDepth(0);
  FixViewport();
}

void BindRenderTarget(const Texture2DRef& pColorTexture, const RenderSurfaceRef& pDepthSurface)
{
	const DXSurfaceRef &pDXColorSurface = pColorTexture->GetSurface(0);
	const DXSurfaceRef &pDXDepthSurface = pDepthSurface->GetDXSurface();
	BindRenderTargetColor(0, pDXColorSurface);
	BindRenderTargetColor(1, 0);
	BindRenderTargetDepth(pDXDepthSurface);
  FixViewport();
}

IDirect3DSurface9* GetRenderTarget(unsigned int renderTargetIndex)
{
	return curColorSurface[renderTargetIndex];
}

IDirect3DSurface9* GetRenderTargetDepth()
{
	return curDepthSurface;
}

int GetRenderTargetWidth()
{
  return curColorSurfaceWidth;
}

int GetRenderTargetHeight()
{
  return curColorSurfaceHeight;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// smirnov [2009/7/15]: it's better to place these functions into Render::Renderer
// just because SmartRenderer is a *CACHE* for dx resources like textures, targets 
// and buffers. It has nothing to do with other render functionality.
// BEGIN {

static int screenshotCount = 0;

void DumpScreenshot( const nstl::string& filename, bool keepAlpha )
{
  // extract extension
  size_t pos = filename.rfind('.');
  string extension = (pos == string::npos) ? string(".bmp") : NStr::GetLower(filename.substr(pos));

  // find proper format
  static struct
  {
    const char* name;
    D3DXIMAGE_FILEFORMAT format;
  } supported[] =
  {
    {".bmp", D3DXIFF_BMP},
    {".jpg", D3DXIFF_JPG},
    {".jpeg",D3DXIFF_JPG},
    {".png", D3DXIFF_PNG},
    {".dds", D3DXIFF_DDS},
    {".hdr", D3DXIFF_HDR},
  };
  D3DXIMAGE_FILEFORMAT format = D3DXIFF_BMP;
  for (int i = 0; i < ARRAY_SIZE(supported); ++i)
    if (extension == supported[i].name)
      format = supported[i].format;

  if (keepAlpha)
  {
    // just write to file backbuffer as is
    HRESULT hr = D3DXSaveSurfaceToFile( filename.c_str(), format, Get(GetRenderer()->GetColorSurface()), 0, 0 );
    NI_DX_THROW(hr, __FUNCTION__);
  }
  else // convert surface to get rid of alpha
  {
    DXSurfacePtr const& pSource = GetRenderer()->GetColorSurface();
    D3DSURFACE_DESC desc;
    HRESULT hr = pSource->GetDesc(&desc);
    NI_DX_THROW(hr, __FUNCTION__);

    desc.Format = D3DFMT_X8R8G8B8;
    desc.MultiSampleType = D3DMULTISAMPLE_NONE;
    desc.MultiSampleQuality = 0;
    RenderSurfaceRef pDest = Create<RenderSurface>(desc);
    hr = D3DXLoadSurfaceFromSurface(Get(pDest->GetDXSurface()), NULL, NULL, Get(pSource), NULL, NULL, D3DX_FILTER_NONE, 0);
    NI_DX_THROW(hr, __FUNCTION__);

    hr = D3DXSaveSurfaceToFile( filename.c_str(), format, Get(pDest->GetDXSurface()), NULL, NULL );
    NI_DX_THROW(hr, __FUNCTION__);
  }

  ++screenshotCount;
}

int GetScreenshotCount()
{
  return screenshotCount;
}

byte* DumpScreenshotToMemory( int width, int height )
{
  int headerSize = 128;
  LPD3DXBUFFER pBuffer = 0;
  D3DXSaveSurfaceToFileInMemory( &pBuffer, D3DXIFF_DDS, Get(Render::GetRenderer()->GetColorSurface()), 0, 0 );
  byte* ptr = (byte*)(pBuffer)->GetBufferPointer();
  unsigned int len = (pBuffer)->GetBufferSize();
  int realLength = len-headerSize;
  NI_ASSERT( width*height*4 == realLength, "Bad Array" );
  byte* realPtr = ptr+headerSize;
  byte* res = new byte[realLength];
  memcpy(res,realPtr,realLength);
  pBuffer->Release();  
  return res;
}

void SetMainViewport( int x, int y, int width, int height ) // smirnov [2009/7/15]: why "Main"?
{
  viewport.X = x;
  viewport.Y = y;
  viewport.Width = width;
  viewport.Height = height;
  viewport.MinZ = 0;
  viewport.MaxZ = 1;

  useViewport = true;
}

void GetMainViewport( int& x, int& y, int& width, int& height )
{
  D3DVIEWPORT9 tempVP;
  D3DVIEWPORT9 *pVP = &viewport;
  if(!useViewport) {
    GetDevice()->GetViewport(&tempVP);
    pVP = &tempVP;
  }

  x = pVP->X;
  y = pVP->Y;
  width = pVP->Width;
  height = pVP->Height;
}

void ResetMainViewport()
{
  HRESULT hr = GetDevice()->SetViewport( &viewport );
  if(s_DXWarnLevel > 1)
    NI_DX_WARN(hr, "ResetMainViewport");
}

void FixViewport() // clip viewport size by depth-stencil's surface size
{
  if(!curDepthSurface)
    return;

  const bool smallViewPort = (curColorSurfaceWidth > viewport.Width || curColorSurfaceHeight > viewport.Height);
  if(useViewport && smallViewPort) {
    HRESULT hr = GetDevice()->SetViewport(&viewport);
    if(s_DXWarnLevel > 1)
      NI_DX_WARN(hr, "FixViewport");
  }
}

void SetUseMainViewport( bool _useViewport ) // smirnov [2009/7/15]: make it return previous value
{
  useViewport = _useViewport; // smirnov [2009/7/15]: who will restore viewport to original value? will wait until next SetRenderTarget()?
}

bool UseMainViewport()
{
  return useViewport;
}

// } END

}; // namespace SmartRenderer
}; // namespace Render

#endif
