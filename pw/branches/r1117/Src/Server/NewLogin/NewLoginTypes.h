#ifndef NEWLOGINTYPES_H_INCLUDED
#define NEWLOGINTYPES_H_INCLUDED

#pragma warning(disable:4238)


#include "Server/RPC/Base.h"
#include "Network/TransportAddress.h"
#include "Network/Address.h"
#include "System/EnumToString.h"
#include "Network/LoginTypes.h"


namespace newLogin
{

namespace serviceIds
{
  const Transport::TServiceId Service = "newlogin";
  static const char * SessionKeySvc   = "newlogin:keys";
} //namespace serviceIds



namespace ESvcConnectionResult
{
  enum Enum
  {
    Ok = 0,
    UnknownSvc,
    Timeout,
    OutOfSvcResources,
    ServerFault,
    WrongFrontendKey
  };

  NI_ENUM_DECL_STD;
}



typedef nstl::fixed_string<char, 64> LoginString;
typedef nstl::fixed_string<char, 64> PasswordString;
typedef nstl::fixed_string<char, 32> SessionKeyString;
typedef nstl::fixed_string<char, 64> WelcomeString;
typedef nstl::fixed_string<char, 32> KeyString;
typedef nstl::fixed_string<char, 64> AddressString;
// Web-session player key: sha256(str(user_id) + sessionToken + api_key) hex, 64 chars
typedef nstl::fixed_string<char, 64> PlayerKeyString;
typedef nstl::fixed_string<char, 64> NicknameString;
typedef nstl::fixed_string<char, 32> FlagIdString;
typedef nstl::fixed_string<char, 96> MapIdString;



struct LoginHello : public rpc::Data
{
  SERIALIZE_ID();

  ZDATA
  ZNOPARENT(rpc::Data)
  int clientRevision;
  int protocolVersion;
  LoginString login;
  PasswordString password;
  SessionKeyString sessionkey;
  PlayerKeyString playerKey;   // 7 — web-session player key (empty = legacy login by nickname)

  ZEND int operator&( IBinSaver &f ) { f.Add(2,&clientRevision); f.Add(3,&protocolVersion); f.Add(4,&login); f.Add(5,&password); f.Add(6,&sessionkey); f.Add(7,&playerKey); return 0; }

  LoginHello()
    : clientRevision( 0 )
    , protocolVersion( 0 )
  {
  }

  LoginHello(const LoginHello& other)
    : rpc::Data(other)
    , clientRevision(other.clientRevision)
    , protocolVersion(other.protocolVersion)
    , login(other.login)
    , password(other.password)
    , sessionkey(other.sessionkey)
    , playerKey(other.playerKey)
  {
  }

  LoginHello& operator=(const LoginHello& other)
  {
    rpc::Data::operator=(other);
    clientRevision = other.clientRevision;
    protocolVersion = other.protocolVersion;
    login = other.login;
    password = other.password;
    sessionkey = other.sessionkey;
    playerKey = other.playerKey;
    return *this;
  }
};


// Web session metadata carried by LoginReply (synchronizer 'connectToWebSession'
// response). 'valid' = the session was found and the player key was accepted.
//
// Only the match metadata is delivered here (which map to create, how many
// slots). Per-player data -- hero, skin, talents, ratings, flag, league,
// recommended stats -- is NOT sent over the login channel: the lobby parses the
// synchronizer record once (Shared/WebSessionParse.h) and delivers it to every
// client as NCore::PlayerInfo (Peered::ClientInfo -> gamesvc -> MapStartInfo),
// so the client keeps no second copy of it.
struct WebSessionData : public rpc::Data
{
  SERIALIZE_ID();

  ZDATA
  ZNOPARENT(rpc::Data)
  bool  valid;
  MapIdString mapId;
  int  playersCount;

  ZEND int operator&( IBinSaver &f )
  {
    f.Add( 2, &valid );
    if ( valid )
    {
      f.Add( 3, &mapId );
      f.Add( 4, &playersCount );
    }
    return 0;
  }

  WebSessionData()
    : valid( false ), playersCount( 0 )
  {
  }
};



struct LoginReply : public rpc::Data
{
  SERIALIZE_ID();

  ZDATA
  ZNOPARENT(rpc::Data)
  Login::ELoginResult::Enum   code;
  Transport::TClientId        uid;
  WelcomeString               welcomingSvcId;
  WebSessionData              webSession;    // 5 — web session data (empty when not a web login)

  ZEND int operator&( IBinSaver &f ) { f.Add(2,&code); f.Add(3,&uid); f.Add(4,&welcomingSvcId); f.Add(5,&webSession); return 0; }

  LoginReply()
    : code( Login::ELoginResult::NoResult )
    , uid( 0 )
  {
  }

  LoginReply(const LoginReply& other)
    : rpc::Data(other)
    , code(other.code)
    , uid(other.uid)
    , welcomingSvcId(other.welcomingSvcId)
    , webSession(other.webSession)
  {
  }

  LoginReply& operator=(const LoginReply& other)
  {
    rpc::Data::operator=(other);
    code = other.code;
    uid = other.uid;
    welcomingSvcId = other.welcomingSvcId;
    webSession = other.webSession;
    return *this;
  }
};



struct ServiceRequest : public rpc::Data
{
  SERIALIZE_ID();

  ZDATA
  ZNOPARENT(rpc::Data)
  int                         requestId;
  Transport::TServiceId       svcId;

  ZEND int operator&( IBinSaver &f ) { f.Add(2,&requestId); f.Add(3,&svcId); return 0; }

  ServiceRequest()
    : requestId( 0 )
  {
  }

  ServiceRequest(const ServiceRequest& other)
    : rpc::Data(other)
    , requestId(other.requestId)
    , svcId(other.svcId)
  {
  }

  ServiceRequest& operator=(const ServiceRequest& other)
  {
    rpc::Data::operator=(other);
    requestId = other.requestId;
    svcId = other.svcId;
    return *this;
  }
};



struct ServiceReqReply : public rpc::Data
{
  SERIALIZE_ID();

  ZDATA
  ZNOPARENT(rpc::Data)
  int                         requestId;
  Transport::TServiceId       svcId;
  ESvcConnectionResult::Enum  code;
  mutable AddressString               externalAddress;
  KeyString                   key;

  ZEND int operator&( IBinSaver &f ) { f.Add(2,&requestId); f.Add(3,&svcId); f.Add(4,&code); f.Add(5,&externalAddress); f.Add(6,&key); return 0; }

  ServiceReqReply()
    : code( ESvcConnectionResult::Ok )
  {
  }

  ServiceReqReply(const ServiceReqReply& other)
    : rpc::Data(other)
    , requestId(other.requestId)
    , svcId(other.svcId)
    , code(other.code)
    , externalAddress(other.externalAddress)
    , key(other.key)
  {
  }

  ServiceReqReply& operator=(const ServiceReqReply& other)
  {
    rpc::Data::operator=(other);
    requestId = other.requestId;
    svcId = other.svcId;
    code = other.code;
    externalAddress = other.externalAddress;
    key = other.key;
    return *this;
  }
};



struct FrontendHello : public rpc::Data
{
  SERIALIZE_ID();

  ZDATA
  ZNOPARENT(rpc::Data)
  Transport::TClientId        uid;
  KeyString                   key;

  ZEND int operator&( IBinSaver &f ) { f.Add(2,&uid); f.Add(3,&key); return 0; }

  FrontendHello()
    : uid( 0 )
  {
  }

  FrontendHello(const FrontendHello& other)
    : rpc::Data(other)
    , uid(other.uid)
    , key(other.key)
  {
  }

  FrontendHello& operator=(const FrontendHello& other)
  {
    rpc::Data::operator=(other);
    uid = other.uid;
    key = other.key;
    return *this;
  }
};



struct FrontendHelloReply : public rpc::Data
{
  SERIALIZE_ID();

  ZDATA
  ZNOPARENT(rpc::Data)
  ESvcConnectionResult::Enum  code;

  ZEND int operator&( IBinSaver &f ) { f.Add(2,&code); return 0; }

  FrontendHelloReply()
    : code( ESvcConnectionResult::Ok )
  {
  }

  FrontendHelloReply(const FrontendHelloReply& other)
    : rpc::Data(other)
    , code(other.code)
  {
  }

  FrontendHelloReply& operator=(const FrontendHelloReply& other)
  {
    rpc::Data::operator=(other);
    code = other.code;
    return *this;
  }
};

} //namespace newLogin

#endif //NEWLOGINTYPES_H_INCLUDED
