#include "stdafx.h"
#include "AOERenderer.h"
#include "MaterialSpec.h"
#include "GlobalMasks.h"
#include "../System/FastMath.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
  DECLARE_NULL_RENDER_FLAG

  struct AOERendererVertex
  {
    float x, y;
    int i;
  };
} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Render
{

AOERenderer* AOERenderer::s_pInstance = NULL;

void AOERenderer::Init()
{
  NI_VERIFY(s_pInstance == NULL, "AOERenderer already inited", return);
  s_pInstance = new AOERenderer();
}

void AOERenderer::Term()
{
  SAFE_DELETE(s_pInstance);
}

AOERenderer* AOERenderer::Get()
{
  return s_pInstance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

AOERenderer::AOERenderer() :
  bInit(false),
  saturation(0.f),
  satColor(0.f,0.f,0.f,0.f),
  saturate(false)
{
  if(!RENDER_DISABLED) {
    if (!CreateQuad() || !CreateFan() || !CreateStrip() || !CreateCurve())
      return;
  }

  for (int i = 0; i < _NUM_CHANNELS; i++)
  {
    AreaChannel& channel = channels[i];
    channel.type = AREA_DISK;
    Reset(channel.pMaterial, NULL);
    channel.color = HDRColor::WHITE();
    channel.radius = 0.0f;
    channel.span = 0.0f;
    Identity(&channel.xform);
    channel.enabled = false;
  }

  bInit = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

AOERenderer::~AOERenderer()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool AOERenderer::CreateQuad()
{
  //  1      0
  //   +----+
  //   |  / |
  //   | /  |
  //   +----+
  //  2      3

  // vertex buffer
  AOERendererVertex vertices[] = {
    {+1.0f, +1.0f, 0},
    {-1.0f, +1.0f, 1},
    {-1.0f, -1.0f, 2},
    {+1.0f, -1.0f, 3}
  };
  DXVertexBufferRef pVB = CreateVB(sizeof(vertices), RENDER_POOL_MANAGED, vertices);
  NI_VERIFY(::Get(pVB), "Unable to create vertex buffer for AOE", return false);

  // index buffer
  unsigned int indices[] = {0, 1, 2, 0, 2, 3};
  DXIndexBufferRef pIB = CreateIB(sizeof(indices), RENDER_POOL_MANAGED, indices);
  NI_VERIFY(::Get(pIB), "Unable to create index buffer for AOE", return false);

  // prepare primitive
  Primitive& geo = quad;
  DipDescriptor& dip = geo.GetDipDescriptor();
  dip.primitiveType = RENDERPRIMITIVE_TRIANGLELIST;
  dip.baseVertexIndex = 0;
  dip.minIndex = 0;
  dip.numVertices = ARRAY_SIZE(vertices);
  dip.startIndex = 0;
  dip.primitiveCount = ARRAY_SIZE(indices) / 3;

  VertexFormatDescriptor formatDescriptor;
  formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 0, VERTEXELEMENTTYPE_FLOAT2, VERETEXELEMENTUSAGE_POSITION, 0) );
  formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 8, VERTEXELEMENTTYPE_SHORT2, VERETEXELEMENTUSAGE_BLENDINDICES, 0) );

  geo.SetVertexStream(pVB, sizeof(AOERendererVertex));
  geo.SetVertexDeclaration(SmartRenderer::GetVertexFormatDeclaration(formatDescriptor));
  geo.SetIndexBuffer(pIB);

  return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool AOERenderer::CreateFan()
{
  //  sector  and  left/right boundaries
  //
  // 2      1       6      5         6 2 5    1
  //  +----+         +----+           +++----+
  //  |  / |         |  / |           |||  / |
  //  | /  |        3| /  |4         3|||4/  |
  // 0+<   |  and    #====#    =>     +0+    |
  //  | \  |        9|  / |A         9|||A\  |
  //  |  \ |         | /  |           |||  \ |
  //  +----+         +----+           +++----+
  // 8      7       C      B         C 8 B    7

  // vertex buffer
  static const float eps = 1e-5f;
  AOERendererVertex vertices[] = {
    { 0.00f,  0.0f, 0},
    {+1.00f, +1.0f, 1},
    { 0.00f, +1.0f, 2},
    {-0.25f,   eps, 3},
    { 0.25f,   eps, 4},
    { 0.25f, +1.0f, 5},
    {-0.25f, +1.0f, 6},
    {+1.00f, -1.0f, 1},
    { 0.00f, -1.0f, 2},
    {-0.25f,  -eps, 3},
    { 0.25f,  -eps, 4},
    { 0.25f, -1.0f, 5},
    {-0.25f, -1.0f, 6},
  };
  NI_STATIC_ASSERT(ARRAY_SIZE(vertices) == 2 * ARRAY_SIZE(fanParams) - 1, SIZES_DO_NOT_MATCH);
  DXVertexBufferRef pVB = CreateVB(sizeof(vertices), RENDER_POOL_MANAGED, vertices);
  NI_VERIFY(::Get(pVB), "Unable to create vertex buffer for AOE", return false);

  // index buffer
  unsigned int indices[] = {0, 8, 7, 0, 7, 1, 0, 1, 2, 3, 4, 5, 3, 5, 6, 12, 11, 10, 12, 10, 9};
  NI_STATIC_ASSERT(ARRAY_SIZE(indices) == 3 * 7, SIZES_DO_NOT_MATCH);
  DXIndexBufferRef pIB = CreateIB(sizeof(indices), RENDER_POOL_MANAGED, indices);
  NI_VERIFY(::Get(pIB), "Unable to create index buffer for AOE", return false);

  // prepare primitive
  Primitive& geo = fan;
  DipDescriptor& dip = geo.GetDipDescriptor();
  dip.primitiveType = RENDERPRIMITIVE_TRIANGLELIST;
  dip.baseVertexIndex = 0;
  dip.minIndex = 0;
  dip.numVertices = ARRAY_SIZE(vertices);
  dip.startIndex = 0;
  dip.primitiveCount = ARRAY_SIZE(indices) / 3;

  VertexFormatDescriptor formatDescriptor;
  formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 0, VERTEXELEMENTTYPE_FLOAT2, VERETEXELEMENTUSAGE_POSITION, 0) );
  formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 8, VERTEXELEMENTTYPE_SHORT2, VERETEXELEMENTUSAGE_BLENDINDICES, 0) );

  geo.SetVertexStream(pVB, sizeof(AOERendererVertex));
  geo.SetVertexDeclaration(SmartRenderer::GetVertexFormatDeclaration(formatDescriptor));
  geo.SetIndexBuffer(pIB);

  return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void AOERenderer::PrepareFan(float angle)
{
  // 2      1       6      5         6 2 5    1
  //  +----+         +----+           +++----+
  //  |  / |         |  / |           |||  / |
  //  | /  |        3| /  |4         3|||4/  |
  // 0+<   |  and    #====#    =>     +0+    |
  //  | \  |         |  / |           ||| \  |
  //  |  \ |         | /  |           |||  \ |
  //  +----+         +----+           +++----+
  //                                           
  NI_STATIC_ASSERT(ARRAY_SIZE(fanParams) == 7, SIZES_DO_NOT_MATCH);
  float sina = sinf(angle);
  float cosa = cosf(angle);
  float tana = tanf(0.5f * angle);
  fanParams[0].Set(0.0f, 0.0f, 0.5f, 0.5f);
  fanParams[1].Set(1.0f, tana, 1.0f, 0.5f - 0.5f * tana);
  fanParams[2].Set(cosa, sina, 0.5f + 0.5f * cosa, 0.5f - 0.5f * sina);

  float dx = 0.125f * sina;
  float dy = 0.125f * cosa;
  fanParams[3].Set(-dx, dy, 0.125f, 0.5f);
  fanParams[4].Set(dx, -dy, 0.375f, 0.5f);
  fanParams[5].Set(cosa+dx, sina-dy, 0.375f, 0.0f);
  fanParams[6].Set(cosa-dx, sina+dy, 0.125f, 0.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool AOERenderer::CreateStrip()
{
  // 0    1  2    3
  //  +----++----+
  //  |  / ||  / |
  //  | /  || /  |
  //  +----++----+
  // 4    5  6    7

  // vertex buffer
  AOERendererVertex vertices[] = {
    { 0.0f, +1.0f, 0},
    { 0.5f, +1.0f, 1},
    { 0.5f, +1.0f, 2},
    { 1.0f, +1.0f, 3},
    { 0.0f, -1.0f, 0},
    { 0.5f, -1.0f, 1},
    { 0.5f, -1.0f, 2},
    { 1.0f, -1.0f, 3},
  };
  NI_STATIC_ASSERT(ARRAY_SIZE(vertices) == 2 * ARRAY_SIZE(stripParams[0]), SIZES_DO_NOT_MATCH);
  DXVertexBufferRef pVB = CreateVB(sizeof(vertices), RENDER_POOL_MANAGED, vertices);
  NI_VERIFY(::Get(pVB), "Unable to create vertex buffer for AOE", return false);

  // index buffer
  unsigned int indices[] = {0, 4, 1, 1, 4, 5, 1, 5, 2, 2, 5, 6, 2, 6, 3, 3, 6, 7};
  NI_STATIC_ASSERT(ARRAY_SIZE(indices) == 3 * 6, SIZES_DO_NOT_MATCH);
  DXIndexBufferRef pIB = CreateIB(sizeof(indices), RENDER_POOL_MANAGED, indices);
  NI_VERIFY(::Get(pIB), "Unable to create index buffer for AOE", return false);

  // prepare primitive
  Primitive& geo = strip;
  DipDescriptor& dip = geo.GetDipDescriptor();
  dip.primitiveType = RENDERPRIMITIVE_TRIANGLELIST;
  dip.baseVertexIndex = 0;
  dip.minIndex = 0;
  dip.numVertices = ARRAY_SIZE(vertices);
  dip.startIndex = 0;
  dip.primitiveCount = ARRAY_SIZE(indices) / 3;

  VertexFormatDescriptor formatDescriptor;
  formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 0, VERTEXELEMENTTYPE_FLOAT2, VERETEXELEMENTUSAGE_POSITION, 0) );
  formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 8, VERTEXELEMENTTYPE_SHORT2, VERETEXELEMENTUSAGE_BLENDINDICES, 0) );

  geo.SetVertexStream(pVB, sizeof(AOERendererVertex));
  geo.SetVertexDeclaration(SmartRenderer::GetVertexFormatDeclaration(formatDescriptor));
  geo.SetIndexBuffer(pIB);

  return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void AOERenderer::PrepareStrip(ChannelType channelId, float width, float base)
{
  // 0    1  2    3
  //  +----++----+
  //  |  / ||  / |
  //  | /  || /  |
  //  +----++----+
  // 
  NI_STATIC_ASSERT(ARRAY_SIZE(stripParams[channelId]) == 4, SIZES_DO_NOT_MATCH);
  float skip = 1.0f <= base ? 0.0f : 1.0f - base;
  float step = 0.5f * (1.0f - skip);

  stripParams[channelId][0].Set(0.0f, width, 0.0f, 0.0f);
  stripParams[channelId][1].Set(step, width, 0.5f, 0.0f);
  stripParams[channelId][2].Set(step + skip, width, 0.5f, 0.0f);
  stripParams[channelId][3].Set(1.0f, width, 1.0f, 0.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool AOERenderer::CreateCurve()
{
  // 0    2    4    6
  //  +----+----+----+   the vertices take their position and uv from the constants of their own index (SetAreaCurve),
  //  |  / |  / |  / |   y < 0 only tells the shader to mirror y and v
  //  | /  | /  | /  |
  //  +----+----+----+
  // 1    3    5    7
  AOERendererVertex vertices[] = {
    {0.0f, +1.0f, 0}, {0.0f, -1.0f, 1},
    {0.0f, +1.0f, 2}, {0.0f, -1.0f, 3},
    {0.0f, +1.0f, 4}, {0.0f, -1.0f, 5},
    {0.0f, +1.0f, 6}, {0.0f, -1.0f, 7},
  };
  DXVertexBufferRef pVB = CreateVB(sizeof(vertices), RENDER_POOL_MANAGED, vertices);
  NI_VERIFY(::Get(pVB), "Unable to create vertex buffer for AOE", return false);

  unsigned int indices[] = {0, 1, 2, 2, 1, 3, 2, 3, 4, 4, 3, 5, 4, 5, 6, 6, 5, 7};
  DXIndexBufferRef pIB = CreateIB(sizeof(indices), RENDER_POOL_MANAGED, indices);
  NI_VERIFY(::Get(pIB), "Unable to create index buffer for AOE", return false);

  Primitive& geo = curve;
  DipDescriptor& dip = geo.GetDipDescriptor();
  dip.primitiveType = RENDERPRIMITIVE_TRIANGLELIST;
  dip.baseVertexIndex = 0;
  dip.minIndex = 0;
  dip.numVertices = ARRAY_SIZE(vertices);
  dip.startIndex = 0;
  dip.primitiveCount = ARRAY_SIZE(indices) / 3;

  VertexFormatDescriptor formatDescriptor;
  formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 0, VERTEXELEMENTTYPE_FLOAT2, VERETEXELEMENTUSAGE_POSITION, 0) );
  formatDescriptor.AddVertexElement( VertexElementDescriptor(0, 8, VERTEXELEMENTTYPE_SHORT2, VERETEXELEMENTUSAGE_BLENDINDICES, 0) );

  geo.SetVertexStream(pVB, sizeof(AOERendererVertex));
  geo.SetVertexDeclaration(SmartRenderer::GetVertexFormatDeclaration(formatDescriptor));
  geo.SetIndexBuffer(pIB);

  return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void AOERenderer::SetAreaCurve(ChannelType channelId, NDb::Material const* pMaterial, CVec2 const* pPoints, int numPoints, float halfWidth, float z, bool usePrimaryColor)
{
  NI_VERIFY(0 <= channelId && channelId < _NUM_CHANNELS, NStr::StrFmt("Invalid AOE index %i of %i maximum", channelId, _NUM_CHANNELS), return);

  float length = 0.0f;
  for (int i = 1; i < numPoints; ++i)
    length += fabs(pPoints[i] - pPoints[i - 1]);
  if (numPoints < 2 || halfWidth <= 0.0f || length < FP_EPSILON)
  {
    ResetArea(channelId);
    return;
  }

  SetAreaParams(channelId, AREA_CURVE, pMaterial, 1.0f, halfWidth);
  AreaChannel& channel = channels[channelId];

  float aspectRatio = 1.0f;
  if (channel.pMaterial)
    if (NDb::AOEMaterial const* pDBMaterial = dynamic_cast<NDb::AOEMaterial const*>(channel.pMaterial->GetDBMaterial()))
      aspectRatio = pDBMaterial->aspectRatio;

  // the lengths of the parts and the u where the path jumps: (s, u before, u after)
  float tail, head, tile = 0.0f;
  vector<CVec3> jumps;
  if (aspectRatio < 0.0f)
  {
    float const full = -aspectRatio * 2.0f * halfWidth; // the path length of the whole texture
    tail = head = 0.25f * full;
    tile = 0.5f * full;
    if (tail + head > length)
      tail = head = 0.5f * length;
    float const bodyEnd = length - head;
    if (bodyEnd - tail > 1e-3f)
    {
      float const d = (bodyEnd - tail) / tile;
      jumps.push_back(CVec3(tail, 0.25f, 0.75f - 0.5f * (d - floorf(d))));
      for (int k = int(floorf(d)); k >= 1; --k)
        if (bodyEnd - k * tile > tail + 1e-3f)
          jumps.push_back(CVec3(bodyEnd - k * tile, 0.75f, 0.25f)); // u grows 0.25 -> 0.75 over a tile
    }
    else
      jumps.push_back(CVec3(tail, 0.25f, 0.75f));
  }
  else
  {
    tail = head = Min(fabs(aspectRatio) * halfWidth, 0.5f * length);
    // the corridor mapping bends at the end of the tail and at the head: points there (a straight path of 2 points
    // stretched the whole texture over one quad - a wedge instead of an arrow)
    jumps.push_back(CVec3(tail, 0.5f, 0.5f));
    if (length - head > tail + 1e-3f)
      jumps.push_back(CVec3(length - head, 0.5f, 0.5f));
  }
  float const bodyEnd = length - head;
  bool const tiled = aspectRatio < 0.0f;

  struct Local
  {
    static void Add(vector<CVec4>& curve, CVec2 const& p, CVec2 t, float halfWidth, float u)
    {
      if (!Normalize(&t))
        t.Set(1.0f, 0.0f);
      CVec2 const n(-t.y * halfWidth, t.x * halfWidth);
      CVec2 const l = p + n;
      CVec2 const r = p - n;
      // the shader gives a vertex with y < 0 the position (x, -y) and the uv (u, 1 - v): the right edge (v = 1) is
      // stored with -y and v = 0
      curve.push_back(CVec4(l.x, l.y, u, 0.0f));
      curve.push_back(CVec4(r.x, -r.y, u, 0.0f));
    }
  };

  channel.curve.clear();
  float s = 0.0f;
  int j = 0;
  for (int i = 0; i < numPoints; ++i)
  {
    if (i > 0)
    {
      CVec2 const seg = pPoints[i] - pPoints[i - 1];
      float const segLen = fabs(seg);
      // the jumps inside this segment
      for (; j < jumps.size() && jumps[j].x < s + segLen; ++j)
      {
        if (jumps[j].x <= s)
          continue;
        CVec2 const p = pPoints[i - 1] + seg * ((jumps[j].x - s) / segLen);
        Local::Add(channel.curve, p, seg, halfWidth, jumps[j].y);
        Local::Add(channel.curve, p, seg, halfWidth, jumps[j].z);
      }
      s += segLen;
    }

    float u = 0.5f;
    if (tail > FP_EPSILON && s < tail)
      u = (tiled ? 0.25f : 0.5f) * s / tail;
    else if (head > FP_EPSILON && s > bodyEnd)
      u = (tiled ? 0.75f : 0.5f) + (tiled ? 0.25f : 0.5f) * (s - bodyEnd) / head;
    else if (tiled)
    {
      float const d = (bodyEnd - s) / tile;
      u = 0.75f - 0.5f * (d - floorf(d));
    }
    Local::Add(channel.curve, pPoints[i], pPoints[Min(i + 1, numPoints - 1)] - pPoints[Max(i - 1, 0)], halfWidth, u);
  }

  SetAreaPosition(channelId, CVec3(0.0f, 0.0f, z), CVec3(1.0f, 0.0f, 0.0f), usePrimaryColor);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void AOERenderer::SetAreaParams(ChannelType channelId, AreaType type, NDb::Material const* pMaterial, float radius, float span)
{
  NI_ASSERT(0 <= channelId && channelId < _NUM_CHANNELS, NStr::StrFmt("Invalid AOE channel index %i of %i maximum", channelId, _NUM_CHANNELS));
  NI_ASSERT(channelId != CHANNEL_DIRECTION || type == AREA_LINE, "Only linee type is supported for direction channel");

  AreaChannel& channel = channels[channelId];
  channel.type = type;
  channel.radius = radius;
  channel.span = span;

  // allow missing material for lazy artists and designers
  if (!pMaterial)
  {
    // just don't show nothing
    Reset(channel.pMaterial, NULL);
  }
  else
  {
    // recreate material only if it has changed
    if (!channel.pMaterial || channel.pMaterial->GetDBMaterial()->GetDBID().GetHashKey() != pMaterial->GetDBID().GetHashKey())
    {
      NI_DATA_VERIFY(pMaterial->GetObjectTypeID() == NDb::AOEMaterial::typeId, NStr::StrFmt("AOE must use AOEMaterial, not '%s'", pMaterial->GetObjectTypeName()), return);
      Reset(channel.pMaterial, static_cast<BaseMaterial*>(CreateRenderMaterial(pMaterial)));
    }
  }

  // rebuild geometry
  if (channel.type == AREA_SECTOR)
    PrepareFan(fabs(channel.span));
  else if ( channel.type == AREA_LINE || channel.type == AREA_WALL )
  {
    SetAreaRadius(channelId, channel.radius);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void AOERenderer::SetAreaRadius(ChannelType channelId, float radius)
{
  NI_ASSERT(0 <= channelId && channelId < _NUM_CHANNELS, NStr::StrFmt("Invalid AOE channel index %i of %i maximum", channelId, _NUM_CHANNELS));

  AreaChannel& channel = channels[channelId];
  channel.radius = radius;

  if ( channel.type == AREA_LINE || channel.type == AREA_WALL )
  {
    // for safety
    if (fabs(channel.radius) < FP_EPSILON)
    {
      channel.radius = 1.0f;
      channel.span = 0.0f;
    }

    float aspectRatio = 1.0f;
    if (channel.pMaterial)
      if (NDb::AOEMaterial const* pDBMaterial = dynamic_cast<NDb::AOEMaterial const*>(channel.pMaterial->GetDBMaterial()))
        aspectRatio = pDBMaterial->aspectRatio;
    float baseLength = aspectRatio * (2.0f * channel.span);
    PrepareStrip(channelId, fabs(channel.span) / channel.radius, fabs(baseLength) / channel.radius);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void AOERenderer::SetAreaPosition(ChannelType channelId, CVec3 const& pos, CVec3 const& dir, bool usePrimaryColor, float interpolateColor /*= -1.0f*/)
{
  NI_ASSERT(0 <= channelId && channelId < _NUM_CHANNELS, NStr::StrFmt("Invalid AOE index %i of %i maximum", channelId, _NUM_CHANNELS));
  AreaChannel& channel = channels[channelId];
  if (channel.type == AREA_DISK)
    channel.xform.Set(pos);
  else
  {
    CVec2 dir2d = (channel.span < 0 ? -dir : dir).AsVec2D();
    if (!Normalize(&dir2d))
      dir2d.Set(1.0f, 0.0f);
    if ( channel.type == AREA_WALL )
    {
      dir2d = CVec2( -dir2d.y, dir2d.x );
      CVec3 newpos = pos - CVec3( dir2d * channel.radius * 0.5f, 0.0f );
      channel.xform.Set( newpos, CQuat(NMath::GetAngle( dir2d ), V3_AXIS_Z) );
    }
    else
    {
      channel.xform.Set(pos, CQuat(NMath::GetAngle(dir2d), V3_AXIS_Z));
    }
  }

  channel.enabled = true;

  HDRColor color(1.0f);
  if (channel.pMaterial)
  {
    if (NDb::AOEMaterial const* pDBMaterial = dynamic_cast<NDb::AOEMaterial const*>(channel.pMaterial->GetDBMaterial()))
    {
      if (interpolateColor < 0.0f)
      {
        color = usePrimaryColor ? pDBMaterial->primaryColor : pDBMaterial->secondaryColor;
      }
      else
      {
        float primaryPercent =  (usePrimaryColor) ? (interpolateColor) : (1.0f - interpolateColor);
        float secondaryPercent =  (!usePrimaryColor) ? (interpolateColor) : (1.0f - interpolateColor);
        color.R = pDBMaterial->primaryColor.R * primaryPercent + pDBMaterial->secondaryColor.R * secondaryPercent;
        color.G = pDBMaterial->primaryColor.G * primaryPercent + pDBMaterial->secondaryColor.G * secondaryPercent;
        color.B = pDBMaterial->primaryColor.B * primaryPercent + pDBMaterial->secondaryColor.B * secondaryPercent;
        color.A = pDBMaterial->primaryColor.A * primaryPercent + pDBMaterial->secondaryColor.A * secondaryPercent;
      }
    }
  }
  channel.color = color;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void AOERenderer::SetAreaColor(ChannelType channelId, HDRColor const& color)
{
  NI_VERIFY(0 <= channelId && channelId < _NUM_CHANNELS, NStr::StrFmt("Invalid AOE index %i of %i maximum", channelId, _NUM_CHANNELS), return);
  channels[channelId].color = color;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void AOERenderer::ResetArea(ChannelType channelId)
{
  NI_ASSERT(0 <= channelId && channelId < _NUM_CHANNELS, NStr::StrFmt("Invalid AOE index %i of %i maximum", channelId, _NUM_CHANNELS));
  AreaChannel& channel = channels[channelId];
  channel.enabled = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void AOERenderer::ResetAll()
{
  for (int i = 0; i < _NUM_CHANNELS; i++)
    channels[i].enabled = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void AOERenderer::Render()
{
  for (int i = 0; i < _NUM_CHANNELS; i++)
  {
    AreaChannel& channel = channels[i];
    if (channel.enabled && channel.pMaterial)
    {
      channel.pMaterial->SetCustomizedGeometryPin(channel.type != AREA_DISK ? NDb::BOOLEANPIN_PRESENT : NDb::BOOLEANPIN_NONE);
      channel.pMaterial->PrepareRenderer();
      GetStatesManager()->SetStateDirect(D3DRS_ZENABLE, 0);
      GetStatesManager()->SetStateDirect(D3DRS_ZWRITEENABLE, 0);

      GetRenderer()->SetVertexShaderConstantsMatrix(WORLD, channel.xform);
      GetRenderer()->SetVertexShaderConstantsFloat(VSHADER_LOCALCONST0, channel.radius);

      if ( saturate )
        GetRenderer()->SetPixelShaderConstantsHDRColor(PSHADER_LOCALCONST0, Render::Saturate( channel.color, saturation, satColor ) );
      else
        GetRenderer()->SetPixelShaderConstantsHDRColor(PSHADER_LOCALCONST0, channel.color );

      switch (channel.type)
      {
      case AREA_DISK:
        quad.Bind();
        quad.Draw();
        break;
      case AREA_SECTOR:
        GetRenderer()->SetVertexShaderConstants(VSHADER_LOCALCONST1, ARRAY_SIZE(fanParams), fanParams);
        fan.Bind();
        fan.Draw();
        break;
      case AREA_LINE:
      case AREA_WALL:
        GetRenderer()->SetVertexShaderConstants(VSHADER_LOCALCONST1, ARRAY_SIZE(stripParams[i]), stripParams[i]);
        strip.Bind();
        strip.Draw();
        break;
      case AREA_CURVE:
        {
          // 4 pairs of vertices (3 quads) a draw, the next draw starts at the last pair; the rest is padded with the last
          // pair (empty quads)
          int const count = channel.curve.size();
          curve.Bind();
          for (int k = 0; k + 2 < count; k += 6)
          {
            CVec4 params[8];
            for (int j = 0; j < 8; ++j)
              params[j] = channel.curve[k + j < count ? k + j : count - 2 + (j & 1)];
            GetRenderer()->SetVertexShaderConstants(VSHADER_LOCALCONST1, ARRAY_SIZE(params), params);
            curve.Draw();
          }
        }
        break;
      default:
        NI_ALWAYS_ASSERT(NStr::StrFmt("Unknown AOE area type %i", channel.type));
      }

      GetStatesManager()->SetStateDirect(D3DRS_ZWRITEENABLE, 1);
      GetStatesManager()->SetStateDirect(D3DRS_ZENABLE, 1);
    }
  }
}

bool AOERenderer::IsActive( ChannelType channelId )
{
  NI_ASSERT(0 <= channelId && channelId < _NUM_CHANNELS, NStr::StrFmt("Invalid AOE index %i of %i maximum", channelId, _NUM_CHANNELS));
  AreaChannel& channel = channels[channelId];
  return (channel.enabled);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

}; // namespace Render

// end of AOERenderer.cpp
