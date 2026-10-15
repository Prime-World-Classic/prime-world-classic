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
// C++03 on purpose: this header is shared with the VS2008 server build.

#include <cstdio>
#include <cstring>
#include <string>

#include "Shared/Sha256.h"

namespace WebSession
{

  const size_t SpectatorKeyHexLength = 64;

  inline bool IsSpectatorPlayerKey( const char * playerKey )
  {
    return playerKey && strlen( playerKey ) > SpectatorKeyHexLength;
  }

  // playerKey: <spectatorKey 64><user_id decimal>. On success 'uid' is the
  // spectator's web user id. An empty keyApi (server not configured) refuses.
  inline bool VerifySpectatorKey( const std::string & sessionToken, const char * playerKey, const char * keyApi, int & uid )
  {
    uid = 0;
    if ( !keyApi || !*keyApi || sessionToken.size() != 32 || !IsSpectatorPlayerKey( playerKey ) )
      return false;

    const char * uidText = playerKey + SpectatorKeyHexLength;
    const size_t uidLength = strlen( uidText );
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

    if ( 0 != strncmp( hexKey, playerKey, SpectatorKeyHexLength ) )
      return false;

    uid = parsedUid;
    return true;
  }

} //namespace WebSession
