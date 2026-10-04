#include "stdafx.h"
#include "ClientAuth.h"
#include "System/SafeTextFormatStl.h"
#include "System/SafeTextFormatNstl.h"
#include <Shared/WebSessionParse.h>
#include <Shared/WebSessionRegistry.h>
#include <Shared/Sha256.h>

NI_DEFINE_REFCOUNT( newLogin::IClientAuth );


namespace newLogin
{

ClientAuth::ClientAuth( IConfigProvider * _config, timer::Time _now ) :
config( _config ),
now( _now )
{
  // Web-session player keys are verified locally against the process registry
  // (the back-end pushes sessions to the game server; see
  // Shared/WebSessionRegistry.h). The shared key for the key formula comes
  // from the same cfg variable the back-end signs its requests with.
  MessageTrace( "Web mode: player keys are verified locally (shared key %s)",
    config->Cfg()->webSessionKey.empty() ? "NOT SET" : "set" );
}



void ClientAuth::Poll( timer::Time _now )
{
  threading::MutexLock lock( mutex );

  now = _now;

  for ( Keys::iterator it = keys.begin(); it != keys.end(); )
    if ( now < it->second.expireTime )
      ++it;
    else
    {
      SessionKey & slot = it->second;
      if ( slot.usage )
        DebugTrace( "Removing session key. uid=%d, key=%s, used=%d", slot.uid, it->first, slot.usage );
      else
        WarningTrace( "Removing unused session key. uid=%d, key=%s", slot.uid, it->first );

      it = keys.erase( it );
    }
}



void ClientAuth::SetLoginAddress( const char * _addr )
{
  threading::MutexLock lock( mutex );

  loginAddress = _addr;
}



void ClientAuth::AddSessionKey( const nstl::string &_sessionKey, const Transport::TServiceId &_sessionPath, const nstl::string &_login, Cluster::TUserId _userid, Cluster::TGameId _gameid, Login::IAddSessionKeyCallback* _pcb )
{
  threading::MutexLock lock( mutex );

  std::string key( _sessionKey.c_str() );

  if ( keys.find( key ) != keys.end() )
    ErrorTrace( "Duplicate session key. uid=%d, key=%s", _userid, _sessionKey );

  SessionKey & slot = keys[key];

  slot.uid = _userid;
  slot.expireTime = now + config->Cfg()->sessionKeyExpire;
  slot.welcomeSvcId = _sessionPath;

  MessageTrace( "Session key added. uid=%d, key=%s, welcome=%s, total_keys=%d", _userid, _sessionKey, _sessionPath.c_str(), keys.size() );

  if ( _pcb )
    _pcb->OnAddSessionKey( 0, loginAddress, _userid );
}



void ClientAuth::AuthorizeClient( LoginReply & _reply, const LoginHello & _hello )
{
  threading::MutexLock lock( mutex );

  if ( _hello.sessionkey.empty() ) {

    WarningTrace( "Empty session key. %s", _hello.login.c_str() );
    _reply.code = Login::ELoginResult::Refused;
    return;
  }

  {
    DevAuth( _reply, _hello );
    return;
  }

  std::string key( _hello.sessionkey.c_str() );

  Keys::iterator it = keys.find( key );
  if ( it == keys.end() )
  {
    WarningTrace( "Unknown session key. key=%s", key.c_str() );
    _reply.code = Login::ELoginResult::Refused;
    return;
  }

  SessionKey & slot = it->second;

  _reply.code = Login::ELoginResult::Success;
  _reply.uid = slot.uid;
  _reply.welcomingSvcId = slot.welcomeSvcId.c_str();

  ++slot.usage;

  MessageTrace( "Client authorized. key=%s, uid=%d, used=%d", key, _reply.uid, slot.usage );
}


// Web-session authorization: the client identifies itself by playerKey
// (sha256(str(user_id)+sessionToken+api_key)) instead of by nickname.
//
// The back-end pushed the session to the process registry when the match was
// confirmed (HttpGateway "web_session_register"), so the whole check is local:
// find the session, verify the key against each of its players. No HTTP is
// involved, and the client makes no web requests either. Only the identity is
// resolved here: per-player data is delivered by the lobby through
// NCore::PlayerInfo (Peered::ClientInfo -> gamesvc -> MapStartInfo, see
// Shared/WebSessionParse.h), so the client keeps no copy of it.
void ClientAuth::DevWebAuth( LoginReply & _reply, const LoginHello & _hello )
{
  _reply.code = Login::ELoginResult::ServerError;

  if ( _hello.sessionkey.length() < 32 )
  {
    _reply.code = Login::ELoginResult::AccessDenied;
    WarningTrace( "Web mode authorization refused. Not valid session key. key_len=%d", (int)_hello.sessionkey.length() );
    return;
  }

  const std::string token( _hello.sessionkey.c_str(), 32 );

  WebSession::Record session;
  if ( !WebSession::Registry::Instance().Find( token, session ) )
  {
    ErrorTrace( "Web session not found in the local registry. token=%s", token.c_str() );
    _reply.code = Login::ELoginResult::AccessDenied;
    return;
  }

  // Verify the presented key against every session player with the same
  // formula the back-end uses to compute it (objects/sessionStore.js).
  const char * keyApi = config->Cfg()->webSessionKey.c_str();
  unsigned char digest[32];
  char hexKey[65];
  char idBuf[16];
  int uid = 0;
  bool matched = false;
  for ( size_t i = 0; i < session.players.size(); ++i )
  {
    const int playerId = session.players[i].id;
    sprintf( idBuf, "%d", playerId );

    WebSha256::Digest digestCalc;
    digestCalc.AddString( idBuf );
    digestCalc.AddString( token.c_str(), (unsigned)token.size() );
    digestCalc.AddString( keyApi );
    WebSha256::ToHex( digestCalc.Final( digest ), hexKey );

    if ( 0 == strcmp( hexKey, _hello.playerKey.c_str() ) )
    {
      uid = playerId;
      matched = true;
      break;
    }
  }
  if ( !matched )
  {
    ErrorTrace( "Invalid player key. token=%s", token.c_str() );
    _reply.code = Login::ELoginResult::AccessDenied;
    return;
  }

  // Match metadata for the lobby phase: which map to create / join and how
  // many slots it needs. Players themselves are not part of the login reply.
  WebSessionData webMatch;
  webMatch.valid = true;
  webMatch.mapId = session.mapId.c_str();
  webMatch.playersCount = (int)session.players.size();

  _reply.code = Login::ELoginResult::Success;
  _reply.uid = uid;
  _reply.webSession = webMatch;

  MessageTrace( "Web mode authorization ok. uid=%d, mapId=%s, players=%d",
    uid, webMatch.mapId.c_str(), webMatch.playersCount );
}


// Player identification on login: the only way in is the playerKey from the
// launch URL. The nickname path (the old dev login) is gone together with the
// synchronizer - no client arrives with a nickname any more.
void ClientAuth::DevAuth( LoginReply & _reply, const LoginHello & _hello )
{
  if ( _hello.playerKey.empty() )
  {
    _reply.code = Login::ELoginResult::Refused;
    WarningTrace( "Authorization refused: no player key (login is ignored)" );
    return;
  }

  DevWebAuth( _reply, _hello );
}

} //namespace newLogin
