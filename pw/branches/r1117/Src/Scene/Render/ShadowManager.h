#pragma once

#if defined(PW_LINUX_NULL_RENDER)

#include "../../System/Singleton4DLL.h"
#include "../../System/GeomMisc.h"
#include "../../Render/aabb.h"
#include "../../Render/DBRender.h"
#include "../../Render/GlobalMasks.h"
#include "../../Render/ShadowReceiverVolume.h"
#include "../../Render/sceneconstants.h"
#include "../../Render/texture.h"

namespace Render
{
  class BatchQueue;

  class ShadowManager : public Singleton4DLL<ShadowManager>
  {
    NDb::ShadowMode shadowMode;
    float depthBias;
    float biasSlope;
    AABB casterBoundsLS;
    CVec3 lightDirWS;
    HDRColor shadowColor;
    float shadowLength;
    float shadowFar;
    float shadowFarRange;
    float shadowFarFinal;
    bool isShadowRendering;
    NDb::ShadowBlendMode blendMode;
    CPtr<IShadowReceiverVolume> shadowReceiverVolume;

  public:
    struct Params
    {
      int fullSizeX;
      int fullSizeY;
      int fullSizeSAX;
      int fullSizeSAY;
      NDb::ShadowMode shadowMode;
      float powerOfESM;
      float depthBias;
      float biasSlope;
      Render::HDRColor shadowColor;
      NDb::Direction shadowDirection;
      NDb::ShadowBlendMode shadowBlendMode;
      float shadowLength;
      float shadowFar;
      float shadowFarRange;

      Params()
        : fullSizeX(2048)
        , fullSizeY(2048)
        , fullSizeSAX(1024)
        , fullSizeSAY(1024)
        , shadowMode(NDb::SHADOWMODE_PCF)
        , powerOfESM(80.0f)
        , depthBias(0.0001f)
        , biasSlope(4.0f)
        , shadowColor(0.0f, 0.0f, 0.1f, 0.25f)
        , shadowDirection()
        , shadowBlendMode(NDb::SHADOWBLENDMODE_LERPBYALPHA)
        , shadowLength(20.0f)
        , shadowFar(60.0f)
        , shadowFarRange(10.0f)
      {
      }

      void FromLightEnvironment(const NDb::LightEnvironment* const lenv)
      {
        if (!lenv)
        {
          return;
        }

        fullSizeX = lenv->fullSizeX;
        fullSizeY = lenv->fullSizeY;
        fullSizeSAX = lenv->fullSizeSAX;
        fullSizeSAY = lenv->fullSizeSAY;
        shadowMode = lenv->shadowMode;
        powerOfESM = lenv->powerOfESM;
        depthBias = lenv->depthBias;
        biasSlope = lenv->biasSlope;
        shadowColor = lenv->shadowColor;
        shadowDirection = lenv->shadowDirection;
        shadowBlendMode = lenv->shadowBlendMode;
        shadowLength = lenv->shadowLength;
        shadowFar = lenv->shadowFar;
        shadowFarRange = lenv->shadowFarRange;
      }

      void FromBlend(const Params& lhs, const Params& rhs, const float t)
      {
        if (t >= 1.0f)
        {
          *this = rhs;
          return;
        }

        *this = lhs;
      }
    };

    ShadowManager()
      : shadowMode(NDb::SHADOWMODE_PCF)
      , depthBias(0.0001f)
      , biasSlope(4.0f)
      , casterBoundsLS()
      , lightDirWS(0.0f, 0.0f, 0.0f)
      , shadowColor(0.0f, 0.0f, 0.1f, 0.25f)
      , shadowLength(20.0f)
      , shadowFar(60.0f)
      , shadowFarRange(10.0f)
      , shadowFarFinal(60.0f)
      , isShadowRendering(false)
      , blendMode(NDb::SHADOWBLENDMODE_LERPBYALPHA)
      , shadowReceiverVolume(0)
    {
    }

    explicit ShadowManager(const NDb::LightEnvironment* lenv)
      : ShadowManager()
    {
      Params params;
      params.FromLightEnvironment(lenv);
      SetRuntimeParams(params);
      shadowMode = params.shadowMode;
      depthBias = params.depthBias;
      biasSlope = params.biasSlope;
      blendMode = params.shadowBlendMode;
    }

    explicit ShadowManager(const Params& params)
      : ShadowManager()
    {
      SetRuntimeParams(params);
      shadowMode = params.shadowMode;
      depthBias = params.depthBias;
      biasSlope = params.biasSlope;
      blendMode = params.shadowBlendMode;
    }

    ~ShadowManager() {}

    bool IsShadowRendering() { return isShadowRendering; }
    void SetShadowRenderingFlag(bool isShadRendering) { isShadowRendering = isShadRendering; }
    void SetShadowReceiverVolume(IShadowReceiverVolume* _shadowReceiverVolume) { shadowReceiverVolume = _shadowReceiverVolume; }

    void BuildLightMatrices(SceneConstants* lightSceneConsts, AABB* _pCasterAABB,
                            const SceneConstants& _viewSceneConsts, const CVec3& _frustumEdge,
                            float _minHeight, float _maxHeight)
    {
      (void)_frustumEdge;
      (void)_minHeight;
      (void)_maxHeight;

      if (lightSceneConsts)
      {
        *lightSceneConsts = _viewSceneConsts;
      }

      if (_pCasterAABB)
      {
        casterBoundsLS = *_pCasterAABB;
      }
      else
      {
        casterBoundsLS = AABB();
      }
    }

    AABB const& GetCastersBoundsLS() const { return casterBoundsLS; }

    void CreateShadowTexture(BatchQueue& _batchQueue, const SceneConstants& _sceneConsts)
    {
      (void)_batchQueue;
      (void)_sceneConsts;
    }

    void ShowShadowTexture() {}

    void ApplyFullscreenShadows(const SceneConstants& viewSceneConsts, Texture2DRef const& pDepthTexture)
    {
      (void)viewSceneConsts;
      (void)pDepthTexture;
    }

    void NoShadows() {}
    void Reload() {}

    void SetMode(NDb::ShadowMode _shadowMode) { shadowMode = _shadowMode; }
    float GetShadowHeight() { return (shadowFar + shadowFarRange) * lightDirWS.z; }

    void SetRuntimeParams(const Params& params)
    {
      shadowColor = params.shadowColor;
      shadowLength = params.shadowLength;
      shadowFar = params.shadowFar;
      shadowFarFinal = params.shadowFar;
      shadowFarRange = params.shadowFarRange;
      blendMode = params.shadowBlendMode;
    }

    static void SubstituteMaterials(BatchQueue& _batchQueue) { (void)_batchQueue; }
    static void RemoveMaterials(BatchQueue& _batchQueue) { (void)_batchQueue; }
  };

} // namespace Render

#else

#include "../../Render/ShadowManager.h"

#endif
