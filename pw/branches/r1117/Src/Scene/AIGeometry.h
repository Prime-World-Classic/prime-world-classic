#pragma once

#include "../System/Color.h"
#include "../System/Geom.h"
#include "../System/nvector.h"

namespace NScene
{
struct SGeometryInstanceDef
{
  CVec3 pos;
  Render::Color color;

  SGeometryInstanceDef()
    : pos(VNULL3)
    , color()
  {
  }
};

class AIGeometry
{
public:
  vector<CVec3> points;
  vector<SEdge> edges;
  vector<STriangle> mesh;
};
}
