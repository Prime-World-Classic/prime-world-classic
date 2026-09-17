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


// Web session player data as returned by the synchronizer 'connectToWebSession'
// ('user' entry). Nickname/flagId are UTF-8 as stored by the synchronizer.
struct WebPlayerData : public rpc::Data
{
  SERIALIZE_ID();

  ZDATA
  ZNOPARENT(rpc::Data)
  int  id;                                 // web user id
  NicknameString nickname;                 // UTF-8
  int  hero;
  int  team;
  int  party;
  int  skin;
  bool muteChat;
  float ratingCurrent, ratingVictory, ratingLoss;
  float ratingAccCurrent, ratingAccVictory, ratingAccLoss;
  int  build[36];                          // talent ids, 0 = empty slot
  int  bar[24];                            // actives: sign = smart cast, |v|-1 = talent slot
  int  profileStats[9];
  int  leagueIdx;
  FlagIdString flagId;

  ZEND int operator&( IBinSaver &f )
  {
    f.Add( 2, &id );
    f.Add( 3, &nickname );
    f.Add( 4, &hero );
    f.Add( 5, &team );
    f.Add( 6, &party );
    f.Add( 7, &skin );
    f.Add( 8, &muteChat );
    f.Add( 9, &ratingCurrent );
    f.Add( 10, &ratingVictory );
    f.Add( 11, &ratingLoss );
    f.Add( 12, &ratingAccCurrent );
    f.Add( 13, &ratingAccVictory );
    f.Add( 14, &ratingAccLoss );
    f.Add( 15, &leagueIdx );
    f.Add( 16, &flagId );
    int i;
    for ( i = 0; i < 36; ++i )
      f.Add( 17 + i, &build[i] );
    for ( i = 0; i < 24; ++i )
      f.Add( 17 + 36 + i, &bar[i] );
    for ( i = 0; i < 9; ++i )
      f.Add( 17 + 36 + 24 + i, &profileStats[i] );
    return 0;
  }

  WebPlayerData()
    : id( 0 ), hero( 0 ), team( 0 ), party( 0 ), skin( 0 ), muteChat( false ),
      ratingCurrent( 0 ), ratingVictory( 0 ), ratingLoss( 0 ),
      ratingAccCurrent( 0 ), ratingAccVictory( 0 ), ratingAccLoss( 0 ),
      leagueIdx( 0 )
  {
    int i;
    for ( i = 0; i < 36; ++i ) build[i] = 0;
    for ( i = 0; i < 24; ++i ) bar[i] = 0;
    for ( i = 0; i < 9; ++i ) profileStats[i] = 0;
  }
};


// Web session data carried by LoginReply (synchronizer 'connectToWebSession'
// response). 'valid' = the session was found and the player key was accepted.
struct WebSessionData : public rpc::Data
{
  SERIALIZE_ID();

  ZDATA
  ZNOPARENT(rpc::Data)
  bool  valid;
  MapIdString mapId;
  nstl::vector<WebPlayerData> players;      // all players of the session (count + elements)

  ZEND int operator&( IBinSaver &f )
  {
    f.Add( 2, &valid );
    if ( valid )
    {
      f.Add( 3, &mapId );
      f.Add( 4, &players );
    }
    return 0;
  }

  WebSessionData()
    : valid( false )
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
