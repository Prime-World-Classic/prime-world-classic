#include "stdafx.h"
#include "ClientAuth.h"
#include "System/SafeTextFormatStl.h"
#include "System/SafeTextFormatNstl.h"
#include <Shared/WebRequests.h>
#include <Shared/WebSessionParse.h>

NI_DEFINE_REFCOUNT( newLogin::IClientAuth );


namespace newLogin
{

ClientAuth::ClientAuth( IConfigProvider * _config, timer::Time _now ) :
config( _config ),
now( _now )
{
  // Web session registry (the backend) address and key. Empty cfg values keep
  // the server_ip.h fallback (local development).
  const Config & cfg = *config->Cfg();
  SetWebSessionEndpoint( cfg.webSessionHost.c_str(), cfg.webSessionPort, cfg.webSessionKey.c_str() );
  MessageTrace( "Web session registry: http://%s:%d", GetWebSessionEndpoint().host.c_str(), GetWebSessionEndpoint().port );
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
// Only the identity is resolved here: the session data itself is fetched by the
// lobby and delivered to every client through NCore::PlayerInfo
// (Peered::ClientInfo -> gamesvc -> MapStartInfo, see Shared/WebSessionParse.h),
// so the client keeps no copy of it and makes no HTTP requests.
void ClientAuth::DevWebAuth( LoginReply & _reply, const LoginHello & _hello )
{
  _reply.code = Login::ELoginResult::ServerError;

  if ( _hello.sessionkey.length() < 32 )
  {
    _reply.code = Login::ELoginResult::AccessDenied;
    WarningTrace( "Web mode authorization refused. Not valid session key. key_len=%d", (int)_hello.sessionkey.length() );
    return;
  }

  const char * token = _hello.sessionkey.c_str();

  std::string cacheKey( token );
  cacheKey += "|";
  cacheKey += _hello.playerKey.c_str();

  {
    // The cache has a TTL: the registry may close the session at any moment
    // (finish / kicked player), so from time to time a fresh lookup is
    // required. 60 seconds keeps relogins cheap and stays consistent.
    const timer::Time kWebSessionCacheTtl = 60.0;
    WebSessionCache::const_iterator it = webSessionCache.find( cacheKey );
    if ( it != webSessionCache.end() && ( it->second.cachedAt + kWebSessionCacheTtl >= now ) )
    {
      _reply.code = Login::ELoginResult::Success;
      _reply.uid = it->second.uid;
      _reply.webSession = it->second.webMatch;
      MessageTrace( "Web mode authorization ok (cache). uid=%d, mapId=%s, players=%d",
        _reply.uid, _reply.webSession.mapId.c_str(), _reply.webSession.playersCount );
      return;
    }
  }

  std::string response = GetWebSessionData( token, _hello.playerKey.c_str() );

  Json::Value parsedValue = WebSession::ParseJson( response.c_str() );
  if ( parsedValue.empty() )
  {
    // Empty response / broken JSON: the backend is unreachable.
    ErrorTrace( "Failed to get web session from the backend. token=%s", token );
    _reply.code = Login::ELoginResult::ServerError;
    return;
  }

  Json::Value errorSet = parsedValue.get( "error", "ERROR" );
  if ( !errorSet.asString().empty() )
  {
    // Session not found or invalid player key.
    ErrorTrace( "Web session lookup failed: %s (token=%s)", errorSet.asString().c_str(), token );
    _reply.code = Login::ELoginResult::AccessDenied;
    return;
  }

  Json::Value playerInfo = parsedValue.get( "playerInfo", Json::Value() );
  WebSession::Player player;
  if ( !WebSession::ParsePlayer( playerInfo, player ) )
  {
    ErrorTrace( "Web session playerInfo is missing or invalid. token=%s", token );
    _reply.code = Login::ELoginResult::ServerError;
    return;
  }

  const Transport::TClientId uid = player.id;

  // Match metadata for the lobby phase: which map to create / join and how many
  // slots it needs. Players themselves are not part of the login reply.
  WebSessionData webMatch;
  webMatch.valid = true;
  const Json::Value mapIdValue = parsedValue.get( "mapId", Json::Value() );
  if ( !mapIdValue.empty() )
    webMatch.mapId = mapIdValue.asString().c_str();
  const Json::Value usersData = parsedValue.get( "usersData", Json::Value() );
  webMatch.playersCount = usersData.isArray() ? (int)usersData.size() : 1;

  {
    WebSessionCacheEntry entry;
    entry.uid = uid;
    entry.webMatch = webMatch;
    entry.cachedAt = now;
    webSessionCache[cacheKey] = entry;

    const size_t CacheCap = 256;
    while ( webSessionCache.size() > CacheCap )
      webSessionCache.erase( webSessionCache.begin() );
  }

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
