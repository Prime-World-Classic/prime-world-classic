#include "stdafx.h"
#include "ReplayControlsController.h"

namespace
{
  const char szReplayCommand[] = "ReplayControlCmd";

  // Time in tenths of a second: the timeline moves smoothly, Flash is called ~10 times a second at most
  int StepsToTenths( int steps, int stepLengthMs )
  {
    return steps < 0 ? -1 : (int)( (__int64)steps * stepLengthMs / 100 );
  }
}

namespace NGameX
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ReplayControlsController::ReplayControlsController( UI::FlashContainer2 * flashWnd, const char* className ) :
  FlashInterface( flashWnd, className ),
  flashContainer( flashWnd ),
  shown( false ),
  lastTime( -2 ),
  lastTotal( -2 ),
  lastTarget( -2 ),
  lastSpeed( -2 ),
  lastPaused( false ),
  lastEnded( false )
{
  flashWnd->AddFSListner( szReplayCommand, this );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ReplayControlsController::~ReplayControlsController()
{
  if ( flashContainer )
    flashContainer->RemoveFSListner( szReplayCommand );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ReplayControlsController::SetState( int playedSteps, int totalSteps, int stepLengthMs, float speed, bool paused, int seekTargetStep, bool ended )
{
  const int time = StepsToTenths( playedSteps, stepLengthMs );
  const int total = StepsToTenths( totalSteps, stepLengthMs );
  const int target = StepsToTenths( seekTargetStep, stepLengthMs );
  const int speedPercent = (int)( speed * 100.0f + 0.5f );

  if ( shown && time == lastTime && total == lastTotal && target == lastTarget && speedPercent == lastSpeed
    && paused == lastPaused && ended == lastEnded )
    return;

  lastTime = time;
  lastTotal = total;
  lastTarget = target;
  lastSpeed = speedPercent;
  lastPaused = paused;
  lastEnded = ended;

  // State first, then show: the panel builds itself with the right values
  CallMethod( "SetReplayState", time, total, target, speedPercent, paused, ended );

  if ( !shown )
  {
    shown = true;
    CallMethod( "ShowReplayControls", true );
  }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ReplayControlsController::OnFSCommand( UI::FlashContainer2* _wnd, const char* listenerID, const char* args, const wchar_t * argsW )
{
  if ( strcmp( listenerID, szReplayCommand ) != 0 || !args || strlen( args ) > 64 )
    return;

  // Flash sends: "pause", "speed_inc", "speed_dec", "speed_rst", "seek <seconds>", "seek_to <0..1>", "dbg <text>"
  if ( strncmp( args, "dbg ", 4 ) == 0 )
    MessageTrace( "replay panel: %s", args + 4 );
  else if ( strcmp( args, "pause" ) == 0 )
    NGlobal::RunCommand( L"replay_pause" );
  else if ( strcmp( args, "speed_inc" ) == 0 )
    NGlobal::RunCommand( L"replay_speed_inc" );
  else if ( strcmp( args, "speed_dec" ) == 0 )
    NGlobal::RunCommand( L"replay_speed_dec" );
  else if ( strcmp( args, "speed_rst" ) == 0 )
    NGlobal::RunCommand( L"replay_speed_rst" );
  else if ( strncmp( args, "seek ", 5 ) == 0 )
    NGlobal::RunCommand( NStr::StrFmtW( L"replay_seek %.2f", NStr::ToFloat( args + 5 ) ) );
  else if ( strncmp( args, "seek_to ", 8 ) == 0 )
    NGlobal::RunCommand( NStr::StrFmtW( L"replay_seek_to %.4f", NStr::ToFloat( args + 8 ) ) );
}

}
