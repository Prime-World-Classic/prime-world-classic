#pragma once
// The platform is selected by NV_LINUX_PLATFORM (defined only in the Linux build);
// everything else is treated as Windows: WIN32 / NV_WIN_PLATFORM are not visible
// to every project in this solution.

// ============================================================================
// Web-session data layer for the SERVER (the 'players' JSON the back-end
// pushes via gateway "web_session_register").
//
// The parsed record (WebSession::Player) is a server-side state; it is
// converted into NCore::PlayerInfo (Shared/WebSessionParse.h::
// ApplyToPlayerInfo) and delivered to every client through
// Peered::ClientInfo -> gamesvc -> MapStartInfo. The client keeps no second
// copy of this data.
//
// The record carries game persistentIds (PvX Data), NOT web indexes: the
// web-id -> persistentId conversion happens on the back-end (MariaDB tables
// persistent_hero / persistent_skin / persistent_talents, see pw-api
// objects/persistentIds.js), so a new hero/skin/talent is a DB row, not a
// server rebuild. Record format (back-end, ver 2.15.5+):
//   {"id":131, "nickname":"...", "hero":"bomber", "team":1, "party":0,
//    "skin":"Bomber_S1", "muteChat":false,
//    "rating":    {"current":2001, "victory":2022, "loss":1995},
//    "ratingAcc": {"current":2001, "victory":2022, "loss":1995},
//    "build":["G123", null, ... 36], "bar":[24], "profileStats":[9],
//    "leagueIdx":7, "flagId":"guide"}
//
//   hero  — hero persistentId (string, required);
//   skin  — skin persistentId (string, "" = default skin);
//   build — 36 entries: talent/class-talent persistentId (string) or null
//           (empty slot); bar/profileStats/rating — as before;
//   buildRefine — optional 36-entry int array aligned with build[]: the
//           refine rate (заточка) of the slot's talent, resolved by the
//           back-end from MariaDB (persistent_talents.refineRate). 0/missing
//           = unknown (the client falls back to its local rarity remap).
// ============================================================================

#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <cstdlib>

#if defined( NV_LINUX_PLATFORM )
#include <iconv.h>
#endif

#include <json/json.h>

#include "Server/RPC/RPC.h"
#include "Core/GameTypes.h"

#include "Shared/WebJson.h"            // ParseJson(), Utf8To*/WideToCp1251()
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
      : id( 0 ), team( 0 ), party( 0 ), muteChat( false )
      , ratingCurrent( 1100 ), ratingVictory( 1100 ), ratingLoss( 1100 )
      , ratingAccCurrent( 1100 ), ratingAccVictory( 1100 ), ratingAccLoss( 1100 )
      , leagueIdx( 0 )
    {
      memset( bar, 0, sizeof( bar ) );
      memset( buildRefine, 0, sizeof( buildRefine ) );
      memset( profileStats, 0, sizeof( profileStats ) );
    }

    int         id;
    std::string nickname;         // UTF-8, as delivered by the back-end
    std::string hero;             // hero persistentId (PvX Data)
    int         team;             // 1..2
    int         party;
    std::string skin;             // skin persistentId ("" = default skin)
    bool        muteChat;
    float       ratingCurrent, ratingVictory, ratingLoss;
    float       ratingAccCurrent, ratingAccVictory, ratingAccLoss;
    std::string build[BUILD_SLOTS];       // talent/class-talent persistentId (PvX Data) or "" = empty slot
    int         buildRefine[BUILD_SLOTS]; // refine rate per build slot (0 = unknown; client fallback)
    int         bar[BAR_SLOTS];           // sign = smart cast, abs(v)-1 = build slot
    int         profileStats[PROFILE_STATS];
    int         leagueIdx;
    std::string flagId;           // UTF-8
  };

  // Lobby-side index of the session players by the web user id. The transport
  // client id IS the web user id (newlogin replies uid = web id, and the fake
  // lobby connections use clientId = web id as well), so the lookup is
  // encoding-independent (no cp1251/UTF-8 nickname matching).
  typedef std::map<int, Player> PlayersById;



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
    if ( hero.empty() || !hero.isString() || hero.asString().empty() )
      return false;
    Json::Value team = playerInfo.get( "team", Json::Value() );
    if ( team.empty() || !team.isInt() )
      return false;
    Json::Value party = playerInfo.get( "party", Json::Value() );
    if ( party.empty() || !party.isInt() )
      return false;
    // Skin is a string too: it may legitimately be empty (default skin), so
    // only the type is checked.
    Json::Value skin = playerInfo.get( "skin", Json::Value() );
    if ( !skin.isString() )
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

  // build[] entry: string (talent/class-talent persistentId) or null (empty
  // slot -> ""). Anything else is normalized to an empty slot as well: the
  // back-end (objects/persistentIds.js) is the only producer of this format.
  inline void FillStringArray( const Json::Value & arr, std::string * dst, int count )
  {
    for ( int i = 0; i < count; ++i )
    {
      const Json::Value v = arr[( unsigned )i];
      dst[i] = ( !v.empty() && v.isString() ) ? v.asString() : std::string();
    }
  }

  // players[] entry -> Player. Returns false for a record that fails CheckPlayerInfo.
  inline bool ParsePlayer( const Json::Value & v, Player & out )
  {
    if ( !CheckPlayerInfo( v ) )
      return false;

    out = Player();
    out.id = v.get( "id", Json::Value() ).asInt();
    out.nickname = v.get( "nickname", Json::Value() ).asString();
    out.hero = v.get( "hero", Json::Value() ).asString();
    out.team = v.get( "team", Json::Value() ).asInt();
    out.party = v.get( "party", Json::Value() ).asInt();
    out.skin = v.get( "skin", Json::Value() ).asString();
    out.muteChat = v.get( "muteChat", Json::Value( false ) ).asBool();

    const Json::Value rating = v.get( "rating", Json::Value() );
    out.ratingCurrent = GetRatingValue( rating, "current", "currentRating" );
    out.ratingVictory = GetRatingValue( rating, "victory", "victoryRating" );
    out.ratingLoss    = GetRatingValue( rating, "loss", "lossRating" );

    const Json::Value ratingAcc = v.get( "ratingAcc", Json::Value() );
    out.ratingAccCurrent = GetRatingValue( ratingAcc, "current", "currentRatingAcc" );
    out.ratingAccVictory = GetRatingValue( ratingAcc, "victory", "victoryRatingAcc" );
    out.ratingAccLoss    = GetRatingValue( ratingAcc, "loss", "lossRatingAcc" );

    FillStringArray( v.get( "build", Json::Value() ), out.build, BUILD_SLOTS );
    // buildRefine is optional (older back-ends do not send it): all zeros
    // then, and the client resolves the refine rate from its local DB.
    const Json::Value buildRefine = v.get( "buildRefine", Json::Value() );
    if ( buildRefine.isArray() )
      FillIntArray( buildRefine, out.buildRefine, BUILD_SLOTS );
    FillIntArray( v.get( "bar", Json::Value() ), out.bar, BAR_SLOTS );
    FillIntArray( v.get( "profileStats", Json::Value() ), out.profileStats, PROFILE_STATS );

    out.leagueIdx = v.get( "leagueIdx", Json::Value( 0 ) ).asInt();
    const Json::Value flagId = v.get( "flagId", Json::Value( "" ) );
    out.flagId = flagId.empty() ? std::string() : flagId.asString();
    return true;
  }


  // -------------------------------------------------- build -> game data
  // players[].build (+ players[].buildRefine) + players[].bar ->
  // NCore::PlayerTalentSet.
  // The entries are already the game persistentIds (the back-end resolves the
  // web indexes, objects/persistentIds.js); the server only maps them to the
  // panel layout and to TalentInfo. refineRate comes from the back-end too
  // (persistent_talents.refineRate -> players[].buildRefine); 0 = unknown and
  // the client falls back to its local rarity remap. The "is it an active
  // ability" check still needs the talent DB, which the lobby does not have,
  // so the client resolves it (see PF_GameLogic/HeroSpawn.cpp).

  // The build is delivered as-is: an empty slot (""/null) leaves a hole in
  // the set; the rest of the build is still used. The client tolerates holes
  // the same way it tolerates bot talent sets (PrepareCustomSet/LoadSet
  // simply skip missing slots), so a player with a partial build gets his
  // real talents instead of the hero's full default set.
  //
  // Entry semantics:
  //   ""  -> empty slot (hole)
  //   else -> talent or class-talent persistentId (a separate name namespace,
  //           a build mixing both is common and both are delivered)
  inline void BuildTalentSet( const Player & p, NCore::PlayerTalentSet & out )
  {
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

        const std::string & talentId = p.build[tIndex2];
        if ( talentId.empty() )
          continue;   // hole: no talent in this slot

        NCore::TalentInfo ti;
        ti.id           = Crc32Checksum().AddString( talentId.c_str() ).Get();
        ti.refineRate   = p.buildRefine[tIndex2];   // 0 = unknown -> client fallback
        ti.actionBarIdx = panelSlot[tIndex2];       // -1 = not placed on the panel
        ti.isInstaCast  = smartCast[tIndex2];       // smart-cast request from the web bar

        out.insert( nstl::pair<const unsigned, NCore::TalentInfo >( tIndex, ti ) );
      }
    }
  }

  // Fills the game-side player record from the web record. The hero, the skin
  // and the talent set are the back-end-delivered persistentIds, consistent
  // with the custom game lineup (the lobby assigns the same hero string to the
  // lineup, see ServerNode::TryCreateWebSession).
  inline void ApplyToPlayerInfo( const Player & p, NCore::PlayerInfo & info )
  {
    info.heroId    = Crc32Checksum().AddString( p.hero.c_str() ).Get();
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

    if ( !p.skin.empty() )
      info.heroSkin = p.skin.c_str();

    info.leagueIndex = p.leagueIdx;
    info.flagId      = p.flagId.c_str();
    info.chatMuted   = p.muteChat;

    info.talents.clear();
    BuildTalentSet( p, info.talents );
  }
} // namespace WebSession
