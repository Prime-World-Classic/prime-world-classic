#ifndef REPLAYCONTROLSCONTROLLER_H_INCLUDED
#define REPLAYCONTROLSCONTROLLER_H_INCLUDED

#include "UI/FlashInterface.h"
#include "UI/FSCommandListner.h"
#include "UI/FlashContainer2.h"

namespace NGameX
{

// Replay panel of the combat UI (main.swf, ReplayControls/ReplayControlsPanel.as): time,
// timeline, back / pause / forward and speed buttons. The state comes from the replay player
// (PW_Client/ReplayTransceiver) through IAdventureScreen::UpdateReplayControls; clicks come
// back as FSCommand "ReplayControlCmd" and run the same console commands as the keys in
// replay.cfg (replay_seek, replay_seek_to, replay_pause, replay_speed_inc/dec/rst).
class ReplayControlsController: public UI::FlashInterface, public UI::IFSCommandListner
{
  NI_DECLARE_REFCOUNT_CLASS_2( ReplayControlsController, UI::FlashInterface, UI::IFSCommandListner )

public:
  ReplayControlsController( UI::FlashContainer2 * flashWnd, const char* className );
  ~ReplayControlsController();

  void SetState( int playedSteps, int totalSteps, int stepLengthMs, float speed, bool paused, int seekTargetStep, bool ended );

private:
  virtual void OnFSCommand( UI::FlashContainer2* _wnd, const char* listenerID, const char* args, const wchar_t * argsW );

  Weak<UI::FlashContainer2> flashContainer;
  bool shown;
  int lastTime;
  int lastTotal;
  int lastTarget;
  int lastSpeed;
  bool lastPaused;
  bool lastEnded;
};

}

#endif //REPLAYCONTROLSCONTROLLER_H_INCLUDED
