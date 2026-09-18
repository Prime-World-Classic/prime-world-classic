#ifndef NEWLOGIN_CLIENTAUTH_H_INCLUDED
#define NEWLOGIN_CLIENTAUTH_H_INCLUDED

#include "ClientAuthInterface.h"
#include "NewLoginConfig.h"
#include "Network/SessionKeyRegisterIface.h"
#include <map>
#include <string>

namespace newLogin
{

class ClientAuth : public IClientAuth, public Login::ISessionKeyRegister, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_3( ClientAuth, IClientAuth, Login::ISessionKeyRegister, BaseObjectMT );

public:
  ClientAuth( IConfigProvider * _config, timer::Time _now );

  void Poll( timer::Time _now );

  void SetLoginAddress( const char * _addr );

protected:
  //IClientAuth
  virtual void AuthorizeClient( LoginReply & _reply, const LoginHello & _hello );

  //Login::ISessionKeyRegister
  virtual void AddSessionKey( const nstl::string &_sessionKey, const Transport::TServiceId &_sessionPath, 
    const nstl::string &_login, Cluster::TUserId _userid, Cluster::TGameId _gameid, Login::IAddSessionKeyCallback* _pcb );

private:
  struct SessionKey
  {
    Transport::TClientId    uid;
    Transport::TServiceId   welcomeSvcId;
    timer::Time             expireTime;
    int                     usage;

    SessionKey() :
    uid( 0 ),
    usage( 0 )
    {}
  };

  typedef std::map<std::string, SessionKey>   Keys;

  // Cache of web-session lookups, keyed by "<sessionToken>|<playerKey>".
  // Only the identity and the match metadata are cached; per-player data is
  // delivered by the lobby through NCore::PlayerInfo.
  struct WebSessionCacheEntry
  {
    Transport::TClientId  uid;
    WebSessionData        webMatch;
    timer::Time           cachedAt;

    WebSessionCacheEntry() : uid( 0 ), cachedAt( 0 ) {}
  };
  typedef std::map<std::string, WebSessionCacheEntry> WebSessionCache;

  StrongMT<IConfigProvider>   config;
  timer::Time                 now;
  Keys                        keys;
  string                      loginAddress;
  WebSessionCache             webSessionCache;
  threading::Mutex            mutex;

  void DevAuth( LoginReply & _reply, const LoginHello & _hello );
  void DevWebAuth( LoginReply & _reply, const LoginHello & _hello );
};

} //namespace newLogin

#endif //NEWLOGIN_CLIENTAUTH_H_INCLUDED
