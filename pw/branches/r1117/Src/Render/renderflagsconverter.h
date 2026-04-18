#pragma once

#if defined(PW_LINUX_NULL_RENDER)

#include "dxutils.h"
#include "renderprimitivetype.h"
#include "vertexelementtype.h"
#include "vertexelementusage.h"
#include "locktype.h"
#include "renderformat.h"

typedef int D3DPRIMITIVETYPE;
typedef unsigned int D3DCOLOR;

enum
{
  D3DPT_POINTLIST = 1,
  D3DPT_LINELIST = 2,
  D3DPT_LINESTRIP = 3,
  D3DPT_TRIANGLELIST = 4,
  D3DPT_TRIANGLESTRIP = 5,
  D3DPT_TRIANGLEFAN = 6,

  D3DDECLUSAGE_POSITION = 0,
  D3DDECLUSAGE_BLENDWEIGHT = 1,
  D3DDECLUSAGE_BLENDINDICES = 2,
  D3DDECLUSAGE_NORMAL = 3,
  D3DDECLUSAGE_PSIZE = 4,
  D3DDECLUSAGE_TEXCOORD = 5,
  D3DDECLUSAGE_TANGENT = 6,
  D3DDECLUSAGE_BINORMAL = 7,
  D3DDECLUSAGE_TESSFACTOR = 8,
  D3DDECLUSAGE_POSITIONT = 9,
  D3DDECLUSAGE_COLOR = 10,
  D3DDECLUSAGE_FOG = 11,
  D3DDECLUSAGE_DEPTH = 12,
  D3DDECLUSAGE_SAMPLE = 13,

  D3DDECLTYPE_FLOAT1 = 0,
  D3DDECLTYPE_FLOAT2 = 1,
  D3DDECLTYPE_FLOAT3 = 2,
  D3DDECLTYPE_FLOAT4 = 3,
  D3DDECLTYPE_D3DCOLOR = 4,
  D3DDECLTYPE_UBYTE4 = 5,
  D3DDECLTYPE_SHORT2 = 6,
  D3DDECLTYPE_SHORT4 = 7,
  D3DDECLTYPE_UBYTE4N = 8,
  D3DDECLTYPE_SHORT2N = 9,
  D3DDECLTYPE_SHORT4N = 10,
  D3DDECLTYPE_USHORT2N = 11,
  D3DDECLTYPE_USHORT4N = 12,
  D3DDECLTYPE_UDEC3 = 13,
  D3DDECLTYPE_DEC3N = 14,
  D3DDECLTYPE_FLOAT16_2 = 15,
  D3DDECLTYPE_FLOAT16_4 = 16,
  D3DDECLTYPE_UNUSED = 17
};

namespace Render
{
D3DPRIMITIVETYPE ConvertPrimitiveType(ERenderPrimitiveType type);
UINT GetVertexElementSize(EVertexElementType type);
BYTE ConvertVertexElementUsage(EVertexElementUsage usage);
BYTE ConvertVertexElementType(EVertexElementType type);
EVertexElementUsage Convert2VertexElementUsage(BYTE usage);
EVertexElementType Convert2VertexElementType(BYTE type);
DWORD ConvertRenderLockType(ERenderLockType type);
D3DFORMAT ConvertRenderFormat(ERenderFormat format);
const char* D3DFormat2Str(DWORD _format);
const char* D3DPool2Str(D3DPOOL _pool);
}; // namespace Render

#else

#include "Vendor/DirectX/Include/d3d9types.h"
#include "renderprimitivetype.h"
#include "vertexelementtype.h"
#include "vertexelementusage.h"
#include "renderstates.h"
#include "renderformat.h"
#include "sampler.h"

namespace Render
{

/// Конвертирование типа примитивов рендера в d3d9
D3DPRIMITIVETYPE ConvertPrimitiveType(ERenderPrimitiveType type);
/// Конвертирование типа элемента вершины в его размер
UINT GetVertexElementSize(EVertexElementType type);
/// Конвертирование типа использования элемента вершины в d3d9
BYTE ConvertVertexElementUsage(EVertexElementUsage usage);
/// Конвертирование типа элемента вершины в d3d9
BYTE ConvertVertexElementType(EVertexElementType type);

/// Конвертирование типа использования элемента вершины из d3d9
EVertexElementUsage Convert2VertexElementUsage(BYTE usage);
/// Конвертирование типа элемента вершины из d3d9
EVertexElementType Convert2VertexElementType(BYTE type);

DWORD ConvertRenderLockType(ERenderLockType type);

/// 
D3DFORMAT ConvertRenderFormat(ERenderFormat format);

/// get format name
const char* D3DFormat2Str(DWORD _format);

/// get pool name
const char* D3DPool2Str(D3DPOOL _pool);

}; // namespace Render
#endif
