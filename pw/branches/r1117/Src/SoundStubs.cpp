#include "stdafx.h"
#ifdef NV_LINUX_PLATFORM
#include "Sound/EventScene.h"
#include "Sound/SoundScene.h"

namespace NSoundScene {
  void FMODEvent::Init(const NDb::DBFMODEventDesc &) {}
  bool FMODEvent::IsVaild() const { return false; }
  FMOD::Event* FMODEvent::GetEvent() const { return 0; }
  bool FMODEvent::PlaySound() { return false; }
  bool FMODEvent::IsAllreadyPlayed() const { return false; }
  bool FMODEvent::Stop(bool) { return false; }
  void FMODEvent::Release() {}
  float FMODEvent::GetDuration(bool) { return 0.0f; }

  void FMODGroup::Init(const nstl::string &, const nstl::string &) {}
  bool FMODGroup::IsValid() const { return false; }
  FMOD::EventGroup* FMODGroup::GetGroup() const { return 0; }
  void FMODGroup::Release() {}

  bool PreCacheGroup(const nstl::string &, bool) { return false; }
  FMOD::Event* EventStart(const NDb::DBFMODEventDesc &) { return 0; }
  bool EventStart(FMOD::Event *) { return false; }
  bool EventPause(const NDb::DBFMODEventDesc &, bool) { return false; }
  bool EventPause(FMOD::Event *, bool) { return false; }
  bool EventStop(const NDb::DBFMODEventDesc &) { return false; }
  bool IsEventPlaying(const NDb::DBFMODEventDesc &) { return false; }
  bool IsEventPlaying(FMOD::Event *) { return false; }
  bool IsEventPaused(FMOD::Event *) { return false; }
  bool GetEventLength(FMOD::Event *, int &, bool) { return false; }

  void EventSceneUpdate(float, const CVec3 &, const CVec3 &, const CVec3 &) {}
  void EventSceneClear() {}
  FMOD::Event* GetEvent(FMOD::EventGroup *, const nstl::string &) { return 0; }

  FMODEvent* CreateFMODEvent() { return 0; }
  void ReleaseFMODEvent(FMODEvent *) {}
  FMODGroup* CreateFMODGroup() { return 0; }
  void ReleaseFMODGroup(FMODGroup *) {}

  void SetScene(NScene::IScene *) {}
  void ActivateSoundScene(int, bool) {}
  int GetActiveSoundScene() { return 0; }

  void SetHeartBeatData(const NDb::DBFMODEventDesc &, const nstl::string &) {}
  void EnableHeartBeat(bool) {}
  void UpdateHeartBeat(float) {}
  void PlayHeartBeat(bool) {}

  void SetAmbientData(const NDb::DBFMODEventDesc &) {}
  void EnableAmbient(bool) {}
  void UpdateAmbient(const nstl::string &, float) {}

  void ApplyReverb(nstl::string &) {}
  void SetMusicMultiplier(float) {}
  void SetGlobalPitchCoeff(float) {}

  void UnPauseMusic() {}
  void PauseMusic() {}
}
#endif