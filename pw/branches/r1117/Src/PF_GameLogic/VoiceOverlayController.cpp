#include "stdafx.h"
#include "VoiceOverlayController.h"
#include "NivalInput/Binds.h"

#include <winsock2.h>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW( IOC_VENDOR, 12 )
#endif

namespace NGameX
{

namespace
{
  // launcher content/modules/voiceGameBridge.js
  const unsigned short LAUNCHER_PORT = 34981;
  const float POLL_PERIOD = 0.1f;
  // No answer for this long: launcher is gone (or too old), hide the overlay.
  const float SILENCE_TIMEOUT = 2.0f;
  const int MAX_DATAGRAM = 8192;
  const int MAX_LINE = 512;

  const char szOverlayCommand[] = "VoiceOverlayCmd";

  // Skips `count` space-separated fields; the rest of the line is a nick that may contain spaces.
  const char * SkipFields( const char * p, int count )
  {
    for ( int i = 0; i < count && *p; ++i )
    {
      while ( *p && *p != ' ' )
        ++p;
      while ( *p == ' ' )
        ++p;
    }
    return p;
  }

  wstring FromUtf8( const char * text )
  {
    wstring result;
    NStr::UTF8ToUnicode( &result, string( text ) );
    return result;
  }

  template<class T>
  bool SamePeers( const nstl::vector<T> & a, const nstl::vector<T> & b )
  {
    if ( a.size() != b.size() )
      return false;
    for ( int i = 0; i < a.size(); ++i )
      if ( !( a[i] == b[i] ) )
        return false;
    return true;
  }


  bool IsGameWindowActive()
  {
    HWND foreground = ::GetForegroundWindow();
    if ( !foreground )
      return false;
    DWORD processId = 0;
    ::GetWindowThreadProcessId( foreground, &processId );
    return processId == ::GetCurrentProcessId();
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

VoiceOverlayController::VoiceOverlayController( UI::FlashContainer2 * flashWnd, const char* className ) :
  FlashInterface( flashWnd, className ),
  socketHandle( INVALID_SOCKET ),
  socketFailed( false ),
  winsockStarted( false ),
  pollTimer( 0.f ),
  silenceTime( 0.f ),
  shown( false ),
  pttHeld( false ),
  unlocked( false ),
  hasCurrent( false )
{
  flashWnd->AddFSListner( szOverlayCommand, this );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Players keep their own input_new.cfg, so a new bind never reaches them from the
// default config. Define it once; the in-game preferences then save it with the rest.
void VoiceOverlayController::EnsureDefaultUnlockBind()
{
  Input::Binds * binds = Input::BindsManager::Instance()->GetBinds();
  if ( !binds || binds->FindCommand( "voice_overlay_unlock_on" ) )
    return;

  NGlobal::RunCommand( L"bindsection adventure_screen", L"input" );
  NGlobal::RunCommand( L"bind voice_overlay_unlock_on 'SHIFT'", L"input" );
  NGlobal::RunCommand( L"bind !voice_overlay_unlock_off 'SHIFT'", L"input" );
  NGlobal::RunCommand( L"bindsection", L"input" );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void VoiceOverlayController::SetUnlocked( bool _unlocked )
{
  if ( unlocked == _unlocked )
    return;
  unlocked = _unlocked;
  if ( shown )
    CallMethod( "SetVoiceOverlayInteractive", unlocked );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

VoiceOverlayController::~VoiceOverlayController()
{
  if ( pttHeld )
    Send( "ptt 0" );
  CloseSocket();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool VoiceOverlayController::OpenSocket()
{
  if ( socketHandle != INVALID_SOCKET )
    return true;
  if ( socketFailed )
    return false;

  WSADATA wsaData;
  if ( ::WSAStartup( MAKEWORD( 2, 2 ), &wsaData ) != 0 )
  {
    socketFailed = true;
    return false;
  }
  winsockStarted = true;

  SOCKET s = ::socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
  if ( s == INVALID_SOCKET )
  {
    socketFailed = true;
    CloseSocket();
    return false;
  }

  u_long nonBlocking = 1;
  ::ioctlsocket( s, FIONBIO, &nonBlocking );

  // Without a launcher every send gets an ICMP "port unreachable", which Windows reports
  // as WSAECONNRESET on the next recvfrom. Turn that off: silence is handled by a timeout.
  BOOL reportReset = FALSE;
  DWORD bytesReturned = 0;
  ::WSAIoctl( s, SIO_UDP_CONNRESET, &reportReset, sizeof( reportReset ), NULL, 0, &bytesReturned, NULL, NULL );

  sockaddr_in local;
  memset( &local, 0, sizeof( local ) );
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
  local.sin_port = 0;
  if ( ::bind( s, (const sockaddr *)&local, sizeof( local ) ) != 0 )
  {
    ::closesocket( s );
    socketFailed = true;
    CloseSocket();
    return false;
  }

  socketHandle = s;
  return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void VoiceOverlayController::CloseSocket()
{
  if ( socketHandle != INVALID_SOCKET )
  {
    ::closesocket( (SOCKET)socketHandle );
    socketHandle = INVALID_SOCKET;
  }
  if ( winsockStarted )
  {
    ::WSACleanup();
    winsockStarted = false;
  }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void VoiceOverlayController::Send( const char * text )
{
  if ( socketHandle == INVALID_SOCKET )
    return;

  sockaddr_in target;
  memset( &target, 0, sizeof( target ) );
  target.sin_family = AF_INET;
  target.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
  target.sin_port = htons( LAUNCHER_PORT );
  ::sendto( (SOCKET)socketHandle, text, (int)strlen( text ), 0, (const sockaddr *)&target, sizeof( target ) );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void VoiceOverlayController::Update( float timeDelta )
{
  if ( !OpenSocket() )
    return;

  ReceiveAll();
  UpdatePushToTalk();

  pollTimer += timeDelta;
  silenceTime += timeDelta;

  if ( pollTimer >= POLL_PERIOD )
  {
    pollTimer = 0.f;
    Send( "poll" );
    // Launcher drops a held push-to-talk if it stops hearing about it (game crash).
    if ( pttHeld )
      Send( "ptt 1" );
  }

  if ( shown && silenceTime > SILENCE_TIMEOUT )
    HideOverlay();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void VoiceOverlayController::ReceiveAll()
{
  char buffer[MAX_DATAGRAM + 1];

  for ( int i = 0; i < 32; ++i )
  {
    sockaddr_in from;
    int fromLen = sizeof( from );
    const int size = ::recvfrom( (SOCKET)socketHandle, buffer, MAX_DATAGRAM, 0, (sockaddr *)&from, &fromLen );
    if ( size <= 0 )
      return;

    if ( from.sin_addr.s_addr != htonl( INADDR_LOOPBACK ) || from.sin_port != htons( LAUNCHER_PORT ) )
      continue;

    buffer[size] = 0;

    State state;
    if ( !ParseState( buffer, size, state ) )
      continue;

    silenceTime = 0.f;
    ApplyState( state );
  }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool VoiceOverlayController::ParseState( const char * data, int size, State & state )
{
  bool header = false;
  const char * p = data;
  const char * end = data + size;

  while ( p < end )
  {
    const char * lineEnd = p;
    while ( lineEnd < end && *lineEnd != '\n' )
      ++lineEnd;

    char line[MAX_LINE];
    const int lineLen = (int)( lineEnd - p );
    const int len = lineLen < MAX_LINE - 1 ? lineLen : MAX_LINE - 1;
    memcpy( line, p, len );
    line[len] = 0;
    if ( len > 0 && line[len - 1] == '\r' )
      line[len - 1] = 0;
    p = lineEnd + 1;

    if ( !header )
    {
      if ( strcmp( line, "PWV 1" ) != 0 )
        return false;
      header = true;
      continue;
    }

    if ( strncmp( line, "cfg ", 4 ) == 0 )
    {
      int enabled = 0, radio = 0;
      char keys[64] = { 0 };
      float x = -1.f, y = -1.f;
      if ( sscanf_s( line + 4, "%d %d %63s %f %f", &enabled, &radio, keys, (unsigned)sizeof( keys ), &x, &y ) < 3 )
        continue;
      state.enabled = enabled != 0;
      state.radioMode = radio != 0;
      state.posX = x;
      state.posY = y;
      state.pttKeys.clear();
      for ( const char * k = keys; *k && *k != '-'; )
      {
        const int vk = atoi( k );
        if ( vk > 0 && vk < 256 )
          state.pttKeys.push_back( vk );
        while ( *k && *k != ',' )
          ++k;
        if ( *k == ',' )
          ++k;
      }
    }
    else if ( strncmp( line, "me ", 3 ) == 0 )
    {
      int mic = 0, speaking = 0;
      if ( sscanf_s( line + 3, "%d %d", &mic, &speaking ) != 2 )
        continue;
      state.micState = mic;
      state.meSpeaking = speaking != 0;
      state.myName = FromUtf8( SkipFields( line + 3, 2 ) );
    }
    else if ( strncmp( line, "p ", 2 ) == 0 )
    {
      Peer peer;
      if ( sscanf_s( line + 2, "%d %d %d", &peer.id, &peer.flags, &peer.volume ) != 3 )
        continue;
      peer.name = FromUtf8( SkipFields( line + 2, 3 ) );
      state.peers.push_back( peer );
    }
  }

  return header;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void VoiceOverlayController::ApplyState( const State & state )
{
  // Keys are needed by push-to-talk even if nothing visible changed.
  const bool firstState = !hasCurrent;
  const bool visible = state.enabled;

  if ( firstState && visible )
    // The saved position is applied once; after that the overlay owns it (dragging).
    CallMethod( "SetVoiceOverlayPosition", state.posX, state.posY );

  const bool changed = firstState
    || current.micState != state.micState
    || current.meSpeaking != state.meSpeaking
    || current.myName != state.myName
    || !SamePeers( current.peers, state.peers );

  current = state;
  hasCurrent = true;

  if ( !visible )
  {
    HideOverlay();
    return;
  }

  if ( changed || !shown )
  {
    nstl::vector<int> ids, flags, volumes;
    nstl::vector<wstring> names;
    for ( int i = 0; i < state.peers.size(); ++i )
    {
      ids.push_back( state.peers[i].id );
      flags.push_back( state.peers[i].flags );
      volumes.push_back( state.peers[i].volume );
      names.push_back( state.peers[i].name );
    }
    CallMethod( "SetVoiceOverlayState", state.micState, state.meSpeaking, state.myName, ids, flags, volumes, names );
  }

  if ( !shown )
  {
    CallMethod( "SetVoiceOverlayInteractive", unlocked );
    CallMethod( "ShowVoiceOverlay", true );
    shown = true;
  }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void VoiceOverlayController::HideOverlay()
{
  if ( !shown )
    return;
  CallMethod( "ShowVoiceOverlay", false );
  shown = false;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool VoiceOverlayController::IsPushToTalkHeld() const
{
  if ( !hasCurrent || !current.enabled || !current.radioMode || current.pttKeys.empty() )
    return false;
  if ( !IsGameWindowActive() )
    return false;
  for ( int i = 0; i < current.pttKeys.size(); ++i )
    if ( !( ::GetAsyncKeyState( current.pttKeys[i] ) & 0x8000 ) )
      return false;
  return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void VoiceOverlayController::UpdatePushToTalk()
{
  const bool held = IsPushToTalkHeld();
  if ( held == pttHeld )
    return;
  pttHeld = held;
  Send( held ? "ptt 1" : "ptt 0" );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void VoiceOverlayController::OnFSCommand( UI::FlashContainer2* _wnd, const char* listenerID, const char* args, const wchar_t * argsW )
{
  if ( strcmp( listenerID, szOverlayCommand ) != 0 || !args )
    return;

  // Flash builds the launcher command itself; pass through only the known ones.
  const bool known = strncmp( args, "mute ", 5 ) == 0
    || strcmp( args, "mic" ) == 0
    || strncmp( args, "vol ", 4 ) == 0
    || strncmp( args, "pos ", 4 ) == 0;
  if ( !known || strlen( args ) > 64 )
    return;

  Send( args );
}

}
