#pragma once

#include "../System/Geom.h"
#include <string.h>

namespace Terrain
{
  struct GridConstants
  {
    struct int2
    {
      int x;
      int y;
    };

    bool valid;

    float metersPerElement;
    int2 sizeInElements;
    int tilesPerElement;
    int texelsPerElement;

    float metersPerTile;
    float metersPerTexel;
    int2 sizeInTiles;
    int2 sizeInTexels;
    CVec3 worldSize;

    GridConstants() { memset(this, 0, sizeof(*this)); }
  };
}
