#pragma once
#ifndef AOERENDERER_H_INCLUDED
#define AOERENDERER_H_INCLUDED

#include "primitive.h"
#include "material.h"

namespace Render
{
class BaseMaterial;

// TODO: This class needs a new name. From the begining it used not only for AOE, 
// but also for any abilty range limitation rendering. Now it also shows direction hint arrow.
class AOERenderer
{
public:
  enum AreaType
  {
    AREA_DISK,
    AREA_SECTOR,
    AREA_LINE,
    AREA_WALL,
    AREA_CURVE, //< a bent strip along points (SetAreaCurve)
    _NUM_AREA_TYPES
  };

  // decided to not implement overcomplicated system and predefine two usages according to the design
  enum ChannelType
  {
    CHANNEL_RANGE,  //< used to show range of the ability to be applied
    CHANNEL_TARGET, //< used to show area affected by the ability
    CHANNEL_DIRECTION, //< used to show direction hint arrow
    CHANNEL_RANGE2, //<used to show alternative range
    CHANNEL_UNIT_RANGE1,
    CHANNEL_UNIT_RANGE2,
    CHANNEL_PING_FIRST, //< ground rings of team pings (AdventureScreen), CHANNEL_PING_COUNT channels
    CHANNEL_PING_LAST = CHANNEL_PING_FIRST + 7,
    CHANNEL_CURVE, //< a bent arrow (Taurus A4_1 ram, AdventureScreen::UpdateRamArrow)
    _NUM_CHANNELS
  };
  enum { CHANNEL_PING_COUNT = CHANNEL_PING_LAST - CHANNEL_PING_FIRST + 1 };

public:
  AOERenderer();
  ~AOERenderer();

  static void Init();
  static void Term();
  static AOERenderer* Get();

  void SetSaturation( float _saturation, const CVec4& _satColor, bool _saturate ) { saturation = _saturation; saturate = _saturate; satColor = _satColor; }

  void SetAreaParams(ChannelType channelId, AreaType type, NDb::Material const* pMaterial, float radius, float span);
  void SetAreaRadius(ChannelType channelId, float radius);
  void SetAreaPosition(ChannelType channelId, CVec3 const& pos, CVec3 const& dir, bool usePrimaryColor, float interpolateColor = -1.0f);
  void SetAreaColor(ChannelType channelId, HDRColor const& color); //< overrides the material color chosen by SetAreaPosition
  // a strip of the width 2 * halfWidth along the points (world x, y at the height z), mapped as a corridor: the tail
  // and the head take aspectRatio * halfWidth of the length each
  void SetAreaCurve(ChannelType channelId, NDb::Material const* pMaterial, CVec2 const* pPoints, int numPoints, float halfWidth, float z, bool usePrimaryColor);
  void ResetArea(ChannelType channelId);
  void ResetAll();

  bool IsActive(ChannelType channelId);

  void Render();

private:
  bool CreateQuad();
  bool CreateFan();
  void PrepareFan(float angle);
  bool CreateStrip();
  void PrepareStrip(ChannelType channelId, float len, float base);
  bool CreateCurve();

private:
  static AOERenderer* s_pInstance;
  bool bInit;
  Primitive quad; //< used for disk rendering
  Primitive fan;  //< used for sector rendering
  Primitive strip;//< used for line rendering
  Primitive curve;//< used for bent line rendering: 4 pairs of vertices = 3 quads a draw

  struct AreaChannel
  {
    AreaType type;
    ScopedPtr<BaseMaterial> pMaterial;
    HDRColor color;
    float radius;
    float span; // either angle (for sector) or length (for line)
    SHMatrix xform;
    bool enabled;
    vector<CVec4> curve; // AREA_CURVE: the left and the right vertex of each point (x, y, u, v as the shader wants them)
  };
  AreaChannel channels[_NUM_CHANNELS];

  static const int NUM_FAN_PARAMS = 7;
  CVec4 fanParams[NUM_FAN_PARAMS]; // x, y, u, v

  static const int NUM_STRIP_PARAMS = 4;
  CVec4 stripParams[_NUM_CHANNELS][NUM_STRIP_PARAMS]; // x, y, u, v

  float saturation;
  CVec4 satColor;
  bool saturate;
};

inline AOERenderer* GetAOERenderer() { return AOERenderer::Get(); }

} // namespace Render

#endif // AOERENDERER_H_INCLUDED
