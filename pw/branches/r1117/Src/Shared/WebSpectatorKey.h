#pragma once
// Web-session spectator key (live match viewing for admins / testers).
//
// The back-end gives the spectator launch protocol only to users it allows
// (pw-api objects/spectator.model.js); the game server checks the key locally,
// with the same shared secret as the player keys, and needs no list of
// spectators in the session record:
//
//   spectatorKey = hex( sha256( "spectator" + str(user_id) + sessionToken + keyApi ) )
//
// Launch token (protocol "spectate"):  <sessionToken 32><spectatorKey 64><user_id decimal>
// The client sends everything after the session token as the login playerKey,
// so a playerKey longer than 64 chars marks a spectator. A player key is
// exactly 64 chars (Shared/Sha256.h), the two never mix.
//
// The login field holds at most 67 chars (LoginHello::playerKey,
// Server/NewLogin/NewLoginTypes.h) and is cut silently, so from user id 1000
// on the full form does not fit. The client then sends the compact form
//   s<user_id decimal>:<first 56 hex chars of spectatorKey>   (<= 67 chars)
// It starts with a non-hex char, so it never looks like a player key. The
// lobby still gets the full launch token (JoinWebGame), which has no limit.
//
// C++03 on purpose: this header is shared with the VS2008 server build.

#include <cstdio>
#include <cstring>
#include <string>

#include "Shared/Sha256.h"

namespace WebSession
{

  const size_t SpectatorKeyHexLength = 64;
  // LoginHello::playerKey capacity (fixed_string<char, 64> keeps 67 chars)
  const size_t LoginPlayerKeyMaxLength = 67;
  const size_t SpectatorCompactHexLength = 56;
  const char SpectatorCompactPrefix = 's';
  const char SpectatorCompactSeparator = ':';

  inline bool IsSpectatorPlayerKey( const char * playerKey )
  {
    return playerKey && ( strlen( playerKey ) > SpectatorKeyHexLength || playerKey[0] == SpectatorCompactPrefix );
  }

  // Login form of a full spectator key <spectatorKey 64><user_id>: as is while
  // it fits the login field, the compact form otherwise.
  inline std::string SpectatorLoginKey( const std::string & playerKey )
  {
    if ( playerKey.size() <= LoginPlayerKeyMaxLength )
      return playerKey;
    std::string compact( 1, SpectatorCompactPrefix );
    compact += playerKey.substr( SpectatorKeyHexLength );
    compact += SpectatorCompactSeparator;
    compact += playerKey.substr( 0, SpectatorCompactHexLength );
    return compact;
  }

  // playerKey: <spectatorKey 64><user_id decimal> or the compact form
  // s<user_id decimal>:<spectatorKey 56>. On success 'uid' is the spectator's
  // web user id. An empty keyApi (server not configured) refuses.
  inline bool VerifySpectatorKey( const std::string & sessionToken, const char * playerKey, const char * keyApi, int & uid )
  {
    uid = 0;
    if ( !keyApi || !*keyApi || sessionToken.size() != 32 || !IsSpectatorPlayerKey( playerKey ) )
      return false;

    const char * uidText = 0;
    size_t uidLength = 0;
    const char * keyText = playerKey;
    size_t keyLength = SpectatorKeyHexLength;
    if ( playerKey[0] == SpectatorCompactPrefix )
    {
      uidText = playerKey + 1;
      const char * separator = strchr( uidText, SpectatorCompactSeparator );
      if ( !separator )
        return false;
      uidLength = separator - uidText;
      keyText = separator + 1;
      keyLength = SpectatorCompactHexLength;
      if ( strlen( keyText ) != keyLength )
        return false;
    }
    else
    {
      uidText = playerKey + SpectatorKeyHexLength;
      uidLength = strlen( uidText );
    }

    if ( uidLength < 1 || uidLength > 9 || uidText[0] == '0' )
      return false;

    int parsedUid = 0;
    for ( size_t i = 0; i < uidLength; ++i )
    {
      if ( uidText[i] < '0' || uidText[i] > '9' )
        return false;
      parsedUid = parsedUid * 10 + ( uidText[i] - '0' );
    }

    WebSha256::Digest digestCalc;
    digestCalc.AddString( "spectator" );
    digestCalc.AddString( uidText, (unsigned)uidLength );
    digestCalc.AddString( sessionToken.c_str(), (unsigned)sessionToken.size() );
    digestCalc.AddString( keyApi );

    unsigned char digest[32];
    char hexKey[65];
    WebSha256::ToHex( digestCalc.Final( digest ), hexKey );

    if ( 0 != strncmp( hexKey, keyText, keyLength ) )
      return false;

    uid = parsedUid;
    return true;
  }

} //namespace WebSession
