#ifndef VOICEOVERLAYCONTROLLER_H_INCLUDED
#define VOICEOVERLAYCONTROLLER_H_INCLUDED

#include "UI/FlashInterface.h"
#include "UI/FSCommandListner.h"
#include "UI/FlashContainer2.h"

namespace NGameX
{

// Voice chat overlay in the combat UI. Voice itself (WebRTC) lives in the launcher;
// the game polls it over a local UDP socket (127.0.0.1:34981, see launcher
// content/modules/voiceGameBridge.js), passes the call state to main.swf
// (MainInterface.SetVoiceOverlay*) and forwards overlay clicks and the push-to-talk
// key back. Without a launcher answer the overlay just stays hidden.
// The overlay lets clicks through to the game; it takes the mouse (mute, volume,
// dragging) only while the voice_overlay_unlock bind is held (SHIFT by default,
// rebindable in the in-game shortcuts menu and in the launcher).
class VoiceOverlayController: public UI::FlashInterface, public UI::IFSCommandListner
{
  NI_DECLARE_REFCOUNT_CLASS_2( VoiceOverlayController, UI::FlashInterface, UI::IFSCommandListner )

public:
  VoiceOverlayController( UI::FlashContainer2 * flashWnd, const char* className );
  ~VoiceOverlayController();

  void Update( float timeDelta );
  void SetUnlocked( bool unlocked );

  // Must run before AdventureScreen registers its handlers: a handler resolves its
  // command by name once, at registration, and never sees a command bound later.
  static void EnsureDefaultUnlockBind();

private:
  struct Peer
  {
    int id;
    int flags;
    int volume;
    wstring name;

    bool operator == ( const Peer & other ) const { return id == other.id && flags == other.flags && volume == other.volume && name == other.name; }
  };

  struct State
  {
    bool enabled;
    bool radioMode;
    int micState;
    bool meSpeaking;
    wstring myName;
    float posX;
    float posY;
    nstl::vector<Peer> peers;
    nstl::vector<int> pttKeys;

    State() : enabled(false), radioMode(false), micState(0), meSpeaking(false), posX(-1.f), posY(-1.f) {}
  };

  virtual void OnFSCommand( UI::FlashContainer2* _wnd, const char* listenerID, const char* args, const wchar_t * argsW );

  bool OpenSocket();
  void CloseSocket();
  void Send( const char * text );
  void ReceiveAll();
  bool ParseState( const char * data, int size, State & state );
  void ApplyState( const State & state );
  void HideOverlay();
  void UpdatePushToTalk();
  bool IsPushToTalkHeld() const;

  UINT_PTR socketHandle;
  bool socketFailed;
  bool winsockStarted;
  float pollTimer;
  float silenceTime;
  bool shown;
  bool pttHeld;
  bool unlocked;
  State current;
  bool hasCurrent;
};

}

#endif //VOICEOVERLAYCONTROLLER_H_INCLUDED
