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
now( _now ),
nextDevUserId( 0 )
{
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
    // The cache has a TTL: the synchronizer may invalidate the session at
    // any moment (finishGame / kicked player), and the legacy path always did
    // a fresh lookup. A 60-second TTL keeps relogins cheap while staying
    // consistent with the synchronizer state.
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
    // Empty response / broken JSON: the synchronizer is unreachable.
    ErrorTrace( "Failed to get web session from the synchronizer. token=%s", token );
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


static nstl::map<nstl::string, int> s_userLoginsToIdMap;
void ClientAuth::DevAuth( LoginReply & _reply, const LoginHello & _hello )
{
  // New web-session path: playerKey is set by the client (from the launcher
  // URL); the identity is resolved from the synchronizer here (server side).
  if ( !_hello.playerKey.empty() )
  {
    DevWebAuth( _reply, _hello );
    return;
  }

  _reply.code = Login::ELoginResult::ServerError;
  if ( _hello.login.empty() )
  {
    _reply.code = Login::ELoginResult::Refused;
    WarningTrace( "Dev mode authorization refused, login is empty" );
    return;
  }

  nstl::map<nstl::string, int>::iterator it = s_userLoginsToIdMap.find(_hello.login);
  if (it == s_userLoginsToIdMap.end()) {
    if (_hello.sessionkey.length() < 32) {
      _reply.code = Login::ELoginResult::AccessDenied;
      WarningTrace( "Dev mode authorization refused. Not valid session key. login=%s", _hello.login );
      return;
    }
    const char* token = _hello.sessionkey.c_str();
    std::string response = GetSessionData(token, false);

    Json::Value parsedValue = WebSession::ParseJson(response.c_str());

    if (parsedValue.empty()) {
      ErrorTrace( "Failed to get info from the synchronizer %s", token );
      return;
    }
    Json::Value errorSet = parsedValue.get("error", "ERROR");
    if (!errorSet.asString().empty()) {
      ErrorTrace( "Error occurred during session creation: %s (%s)", errorSet.asString().c_str(), token );
      return;
    }
    Json::Value usersData = parsedValue.get("usersData", Json::Value());
    if (usersData.empty() || !usersData.isArray()) {
      ErrorTrace( "Error occurred during session creation: Empty usersData %s", token );
      return;
    }

    // Diagnostics: the per-service "newlogin" log channel is not routed on
    // Linux, so use the untagged trace macros that reach the main log.
    MessageTrace( "DevAuth: login_len=%d login0=0x%02X sessionkey_len=%zu response_len=%zu users=%u",
      (int)_hello.login.size(), (unsigned char)_hello.login[0], _hello.sessionkey.size(), response.size(), (unsigned)usersData.size() );

    int playersCount = 0;
    Json::Value curPlayer = usersData[playersCount];
    while (!curPlayer.empty()) {
      if (!WebSession::CheckPlayerInfo(curPlayer)) {
        ErrorTrace( "DevAuth: CheckPlayerInfo failed for usersData[%d]",
          playersCount );
        return;
      }

      nstl::string curNickname = WebSession::Utf8ToCp1251(curPlayer.get("nickname", Json::Value()).asString()).c_str();
      int userWebId = curPlayer.get("id", Json::Value()).asInt();

      MessageTrace( "DevAuth: usersData[%d] nickname=%s expected=%s id=%d",
        playersCount, curNickname.c_str(), (_hello.login.c_str() + 1), userWebId );

      if (curNickname == _hello.login.c_str() + 1) {
        s_userLoginsToIdMap[_hello.login] = userWebId;

        _reply.code = Login::ELoginResult::Success;
        _reply.uid = userWebId;
        return;
      }

      playersCount++;
      curPlayer = usersData[playersCount];
    }

    it = s_userLoginsToIdMap.find(_hello.login);
  }
  if (it == s_userLoginsToIdMap.end()) {
    _reply.code = Login::ELoginResult::AccessDenied;
    ErrorTrace( "Dev mode authorization failed! login=%s", _hello.login );
    return;
  }

/*
  unsigned firstDevUid = config->Cfg()->firstDevUid;

  if ( !firstDevUid )
  {
    _reply.code = Login::ELoginResult::AccessDenied;
    WarningTrace( "Dev mode authorization refused. login=%s", _hello.login );
    return;
  }
*/

  _reply.code = Login::ELoginResult::Success;
  _reply.uid = it->second;
/*
  if ( !nextDevUserId )
    nextDevUserId = firstDevUid;

  if ( !RestoreDevAuth( _reply, _hello ) )
    _reply.uid = nextDevUserId++;
*/
  MessageTrace( "Dev mode authorization ok. login=%s, uid=%d", _hello.login, _reply.uid );
}



bool ClientAuth::RestoreDevAuth( LoginReply & _reply, const LoginHello & _hello )
{
  if ( _hello.login[0] != '_' )
    return false;

  std::string login( _hello.login.c_str() );
  DevLoginHistory::iterator it = devLoginHistory.find( login );
  if ( it != devLoginHistory.end() )
  {
    _reply.uid = it->second;
    DebugTrace( "Restored dev mode uid. login=%s, uid=%d", _hello.login, _reply.uid );
  }
  else
  {
    CleanupDevLoginHistory();

    _reply.uid = nextDevUserId++;
    devLoginHistory[login] = _reply.uid;
  }
  return true;
}



void ClientAuth::CleanupDevLoginHistory()
{
  const size_t HistoryCap = 100;

  while ( devLoginHistory.size() >= HistoryCap )
    devLoginHistory.erase( devLoginHistory.begin() );
}

} //namespace newLogin
