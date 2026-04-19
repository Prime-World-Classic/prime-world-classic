#pragma once
#include "SceneComponent.h"

class DiAnimGraph;

namespace NScene
{

class TimeCtrlSceneComponent : public SceneComponent
{
public:
#if defined(PW_LINUX_NULL_RENDER)
	typedef NDb::DBSceneComponent NDbType;
#else
	typedef NDb::DBTimeCtrlSceneComponent NDbType;
#endif

  typedef void (*Callback)(TimeCtrlSceneComponent &tcsc, void *pData);

  enum State
  {
    ST_INACTIVE = 0,

    ST_PLAY,
    ST_FADE_OUT
  };

  enum Action
  {
    ACTION_ACTIVATE = 0,
    ACTION_DEACTIVATE
  };

#if defined(PW_LINUX_NULL_RENDER)
	TimeCtrlSceneComponent()
    : SceneComponent()
    , duration(-1.f)
    , loopTime(1.f)
    , curLocalTime(0.f)
    , lastUpdateLocalTime(0.f)
    , numLoops(0)
    , numLoopsToPlay(0)
    , activeState(ST_INACTIVE)
    , activationTime(0.f)
    , onDeactivateCB(NULL)
    , pUserData(NULL)
  {}

  TimeCtrlSceneComponent(const NDb::DBSceneComponent* pObject, const NDb::AttachedSceneComponent* pObj, const Placement& pos)
    : SceneComponent(pObject, pObj, pos)
    , duration(-1.f)
    , loopTime(1.f)
    , curLocalTime(0.f)
    , lastUpdateLocalTime(0.f)
    , numLoops(0)
    , numLoopsToPlay(0)
    , activeState(ST_INACTIVE)
    , activationTime(0.f)
    , onDeactivateCB(NULL)
    , pUserData(NULL)
  {}
#else
	TimeCtrlSceneComponent() : activeState(ST_INACTIVE), onDeactivateCB(NULL), SceneComponent(0, 0) { Activate(); }

  TimeCtrlSceneComponent(const NDb::DBTimeCtrlSceneComponent* pObject, const NDb::AttachedSceneComponent* pObj, const Placement& pos)
    : SceneComponent(pObj, pObject),
			activeState(ST_INACTIVE),
      onDeactivateCB(NULL),
      duration(-1.f),
      loopTime(1.f),
      curLocalTime(0.f),
      lastUpdateLocalTime(0.f),
      numLoops(0),
      numLoopsToPlay(0)
  {
    pDBObject = pObject;
  }
#endif

  ~TimeCtrlSceneComponent() {}

  virtual void OnAfterAttached();
	virtual void Update( UpdatePars &pars, const Placement& parentPos, float timeDiff );
  virtual void RenderToQueue( class Render::BatchQueue& queue, const struct Render::SceneConstants& sceneConstants ) {}

  State GetActiveState() const { return activeState; }
  void Activate();
  void Deactivate();

  bool SetupTriggeredAction(NDb::EAnimEventType evt, char const *name, float param, Action act);
  void SetOnDeactivateCB(Callback cb, void *pData) { onDeactivateCB = cb; pUserData = pData;}

  virtual bool IsTraversable() const { return (activeState != ST_INACTIVE); }

protected:
  DiAnimGraph *GetAnimGraph();

#if !defined(PW_LINUX_NULL_RENDER)
  NDb::Ptr<NDb::DBTimeCtrlSceneComponent> pDBObject;
#endif

  float   duration;
  float   loopTime;

  // run-time values
  float   curLocalTime;
  float   lastUpdateLocalTime;
  int     numLoops;
  int     numLoopsToPlay;
  
  State   activeState;
  float   activationTime;

  Callback     onDeactivateCB;
  void        *pUserData;
};

}
