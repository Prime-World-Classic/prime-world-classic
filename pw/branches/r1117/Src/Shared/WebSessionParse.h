#pragma once
// ============================================================================
// Web-session data layer for the SERVER (synchronizer 'usersData' JSON).
// Replaces PF_GameLogic/WebLauncher.h.
//
// Only the server talks to the synchronizer (Shared/WebRequests.h). The parsed
// record (WebSession::Player) is a server-side state; it is converted into
// NCore::PlayerInfo (Shared/WebSessionParse.h::ApplyToPlayerInfo) and delivered
// to every client through Peered::ClientInfo -> gamesvc -> MapStartInfo. The
// client keeps no second copy of this data and no web indexes.
//
// Synchronizer record (verified against sync logs / client_sync.py):
//   {"id":131, "nickname":"...", "hero":65, "team":1, "party":0, "skin":2,
//    "muteChat":false,
//    "rating":    {"current":2001, "victory":2022, "loss":1995},
//    "ratingAcc": {"current":2001, "victory":2022, "loss":1995},
//    "build":[36], "bar":[24], "profileStats":[9], "leagueIdx":7, "flagId":"guide"}
// ============================================================================

#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <cstdlib>

#if !defined( NV_WIN_PLATFORM )
#include <iconv.h>
#endif

#include <json/json.h>

#include "Server/RPC/RPC.h"
#include "Core/GameTypes.h"

#include "Shared/WebJson.h"            // ParseJson(), Utf8To*/WideToCp1251()
#include "Shared/WebTalentNames.h"   // talentsMap[]  : web talent id -> persistent name
#include "Shared/WebSkinNames.h"     // WebSession::ResolveSkin()
#include "System/Crc32Checksum.h"    // Crc32Checksum


namespace WebSession
{
  // Layout of the web build: 6 levels x 6 slots (matches the 36 entries the
  // back-end sends in usersData[].build) and 24 panel slots of usersData[].bar.
  static const int BUILD_SLOTS    = 36;
  static const int BAR_SLOTS      = 24;
  static const int PROFILE_STATS  = 9;
  static const int TALENT_LEVELS  = 6;
  static const int TALENT_SLOTS   = BUILD_SLOTS / TALENT_LEVELS;

  // One player of a web session, exactly as the synchronizer stores it.
  struct Player
  {
    Player()
      : id( 0 ), hero( 0 ), team( 0 ), party( 0 ), skin( 0 ), muteChat( false )
      , ratingCurrent( 1100 ), ratingVictory( 1100 ), ratingLoss( 1100 )
      , ratingAccCurrent( 1100 ), ratingAccVictory( 1100 ), ratingAccLoss( 1100 )
      , leagueIdx( 0 )
    {
      memset( build, 0, sizeof( build ) );
      memset( bar, 0, sizeof( bar ) );
      memset( profileStats, 0, sizeof( profileStats ) );
    }

    int         id;
    std::string nickname;         // UTF-8, as stored by the synchronizer
    int         hero;             // 1-based index into the lobby heroes[] table
    int         team;             // 1..2
    int         party;
    int         skin;             // 1-based index into WebSession::ResolveSkin()
    bool        muteChat;
    float       ratingCurrent, ratingVictory, ratingLoss;
    float       ratingAccCurrent, ratingAccVictory, ratingAccLoss;
    int         build[BUILD_SLOTS];       // talent ids, 0 = empty slot
    int         bar[BAR_SLOTS];           // sign = smart cast, abs(v)-1 = build slot
    int         profileStats[PROFILE_STATS];
    int         leagueIdx;
    std::string flagId;           // UTF-8
  };

  // Lobby-side index of the session players by (wide) nickname.
  typedef std::map<std::wstring, Player> PlayersByNickname;



  // Numeric getter that accepts both key spellings used by the back-end
  // ("current"/"victory"/"loss" and "currentRating"/... ).
  inline float GetRatingValue( const Json::Value & obj, const char * key, const char * altKey )
  {
    Json::Value v = obj.get( key, Json::Value() );
    if ( v.empty() )
      v = obj.get( altKey, Json::Value() );
    return v.empty() ? 0.f : v.asFloat();
  }

  inline bool CheckPlayerInfo( const Json::Value & playerInfo )
  {
    if ( playerInfo.empty() )
      return false;

    Json::Value nickname = playerInfo.get( "nickname", Json::Value() );
    if ( nickname.empty() || !nickname.isString() )
      return false;
    Json::Value userId = playerInfo.get( "id", Json::Value() );
    if ( userId.empty() || !userId.isInt() )
      return false;
    Json::Value hero = playerInfo.get( "hero", Json::Value() );
    if ( hero.empty() || !hero.isInt() )
      return false;
    Json::Value team = playerInfo.get( "team", Json::Value() );
    if ( team.empty() || !team.isInt() )
      return false;
    Json::Value party = playerInfo.get( "party", Json::Value() );
    if ( party.empty() || !party.isInt() )
      return false;
    Json::Value skin = playerInfo.get( "skin", Json::Value() );
    if ( skin.empty() || !skin.isInt() )
      return false;

    const Json::Value rating = playerInfo.get( "rating", Json::Value() );
    const Json::Value ratingAcc = playerInfo.get( "ratingAcc", Json::Value() );
    if ( rating.empty() || ratingAcc.empty() )
      return false;
    if ( GetRatingValue( rating, "current", "currentRating" ) == 0.f && rating.get( "current", Json::Value() ).empty() )
      return false;
    if ( GetRatingValue( ratingAcc, "current", "currentRatingAcc" ) == 0.f && ratingAcc.get( "current", Json::Value() ).empty() )
      return false;

    if ( !playerInfo.get( "build", Json::Value() ).isArray() )
      return false;
    if ( !playerInfo.get( "bar", Json::Value() ).isArray() )
      return false;
    return true;
  }

  inline void FillIntArray( const Json::Value & arr, int * dst, int count )
  {
    for ( int i = 0; i < count; ++i )
    {
      const Json::Value v = arr[( unsigned )i];
      dst[i] = v.empty() ? 0 : v.asInt();
    }
  }

  // usersData[] entry -> Player. Returns false for a record that fails CheckPlayerInfo.
  inline bool ParsePlayer( const Json::Value & v, Player & out )
  {
    if ( !CheckPlayerInfo( v ) )
      return false;

    out = Player();
    out.id = v.get( "id", Json::Value() ).asInt();
    out.nickname = v.get( "nickname", Json::Value() ).asString();
    out.hero = v.get( "hero", Json::Value() ).asInt();
    out.team = v.get( "team", Json::Value() ).asInt();
    out.party = v.get( "party", Json::Value() ).asInt();
    out.skin = v.get( "skin", Json::Value() ).asInt();
    out.muteChat = v.get( "muteChat", Json::Value( false ) ).asBool();

    const Json::Value rating = v.get( "rating", Json::Value() );
    out.ratingCurrent = GetRatingValue( rating, "current", "currentRating" );
    out.ratingVictory = GetRatingValue( rating, "victory", "victoryRating" );
    out.ratingLoss    = GetRatingValue( rating, "loss", "lossRating" );

    const Json::Value ratingAcc = v.get( "ratingAcc", Json::Value() );
    out.ratingAccCurrent = GetRatingValue( ratingAcc, "current", "currentRatingAcc" );
    out.ratingAccVictory = GetRatingValue( ratingAcc, "victory", "victoryRatingAcc" );
    out.ratingAccLoss    = GetRatingValue( ratingAcc, "loss", "lossRatingAcc" );

    FillIntArray( v.get( "build", Json::Value() ), out.build, BUILD_SLOTS );
    FillIntArray( v.get( "bar", Json::Value() ), out.bar, BAR_SLOTS );
    FillIntArray( v.get( "profileStats", Json::Value() ), out.profileStats, PROFILE_STATS );

    out.leagueIdx = v.get( "leagueIdx", Json::Value( 0 ) ).asInt();
    const Json::Value flagId = v.get( "flagId", Json::Value( "" ) );
    out.flagId = flagId.empty() ? std::string() : flagId.asString();
    return true;
  }


  // -------------------------------------------------- web indexes -> game data
  // usersData[].build + usersData[].bar -> NCore::PlayerTalentSet.
  // talentsMap[] resolves the web talent id to a persistent name; refineRate and
  // the "is it an active ability" check need the talent DB, which the lobby does
  // not have, so the client resolves them (see PF_GameLogic/HeroSpawn.cpp).

  // A build is authoritative only when it is complete and every id is known: a
  // single empty slot means the player has no finished build and the hero's
  // default talent set is used (the same rule the client applied before the data
  // moved server-side).
  inline bool IsTalentBuildUsable( const Player & p )
  {
    const int namesCount = ( int )( sizeof( talentsMap ) / sizeof( talentsMap[0] ) );
    for ( int i = 0; i < BUILD_SLOTS; ++i )
    {
      if ( p.build[i] <= 0 )
        return false;
      if ( p.build[i] - 1 >= namesCount )
        return false;
    }
    return true;
  }

  inline void BuildTalentSet( const Player & p, NCore::PlayerTalentSet & out )
  {
    if ( !IsTalentBuildUsable( p ) )
      return;   // stays empty -> hero defaults

    // bar[a] != 0 -> build slot abs(bar[a])-1 is placed on panel slot a, sign = smart cast.
    int  panelSlot[BUILD_SLOTS];
    bool smartCast[BUILD_SLOTS];
    for ( int i = 0; i < BUILD_SLOTS; ++i )
    {
      panelSlot[i] = -1;
      smartCast[i] = false;
    }
    for ( int a = 0; a < BAR_SLOTS; ++a )
    {
      const int v = p.bar[a];
      if ( v == 0 )
        continue;
      const int buildIdx = abs( v ) - 1;
      if ( buildIdx >= 0 && buildIdx < BUILD_SLOTS )
      {
        panelSlot[buildIdx] = a;
        smartCast[buildIdx] = v < 0;
      }
    }

    for ( int level = 0; level < TALENT_LEVELS; ++level )
    {
      for ( int slot = 0; slot < TALENT_SLOTS; ++slot )
      {
        // The web build is stored bottom-up relative to the in-game grid (the
        // same index math the client used before).
        const unsigned tIndex  = ( unsigned )( level * TALENT_SLOTS + slot + 1 );
        const unsigned tIndex2 = ( unsigned )( ( TALENT_LEVELS - 1 - level ) * TALENT_SLOTS + slot );

        NCore::TalentInfo ti;
        ti.id           = Crc32Checksum().AddString( talentsMap[p.build[tIndex2] - 1] ).Get();
        ti.refineRate   = 0;                        // resolved from the DB by the client
        ti.actionBarIdx = panelSlot[tIndex2];       // -1 = not placed on the panel
        ti.isInstaCast  = smartCast[tIndex2];       // smart-cast request from the web bar

        out.insert( nstl::pair<const unsigned, NCore::TalentInfo >( tIndex, ti ) );
      }
    }
  }

  // Fills the game-side player record from the web record. heroPersistentId is the
  // hero the lobby assigns to this player (its own heroes[] table), so the hero id,
  // the skin and the talent set are all consistent with the custom game lineup.
  inline void ApplyToPlayerInfo( const Player & p, const std::string & heroPersistentId, NCore::PlayerInfo & info )
  {
    info.heroId    = Crc32Checksum().AddString( heroPersistentId.c_str() ).Get();
    info.heroRating        = p.ratingCurrent;
    info.playerRating      = p.ratingCurrent;
    info.ratingDeltaPrediction.onVictory = p.ratingVictory - p.ratingCurrent;
    info.ratingDeltaPrediction.onDefeat  = p.ratingLoss    - p.ratingCurrent;
    info.ratingAccCurrent  = p.ratingAccCurrent;
    info.ratingAccVictory  = p.ratingAccVictory;
    info.ratingAccLoss     = p.ratingAccLoss;

    info.profileStats.resize( PROFILE_STATS );
    for ( int i = 0; i < PROFILE_STATS; ++i )
      info.profileStats[i] = p.profileStats[i];

    if ( p.skin > 0 )
      info.heroSkin = ResolveSkin( heroPersistentId, p.skin - 1 ).c_str();

    info.leagueIndex = p.leagueIdx;
    info.flagId      = p.flagId.c_str();
    info.chatMuted   = p.muteChat;

    info.talents.clear();
    BuildTalentSet( p, info.talents );
  }
} // namespace WebSession
