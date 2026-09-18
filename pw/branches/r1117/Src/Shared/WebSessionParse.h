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

#include "Shared/WebTalentNames.h"   // talentsMap[]  : web talent id -> persistent name
#include "Shared/WebSkinNames.h"     // WebSession::ResolveSkin()
#include "Shared/shared_data.h"      // ConvertFromClassID()
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


  // ---------------------------------------------------------------- encodings
  // The synchronizer stores UTF-8; the game works with wide (UTF-16) nicknames
  // and CP1251 narrow strings. Implementations are moved out of WebLauncher.h
  // unchanged (Win32 API on Windows, iconv on Linux).
#if defined( NV_WIN_PLATFORM )
  inline std::wstring Utf8ToWide( const std::string & utf8String )
  {
    int utf8Length = static_cast<int>( utf8String.length() );
    int wideCharLength = MultiByteToWideChar( CP_UTF8, 0, utf8String.c_str(), utf8Length, NULL, 0 );
    std::wstring wideCharString;
    wideCharString.resize( wideCharLength );
    MultiByteToWideChar( CP_UTF8, 0, utf8String.c_str(), utf8Length, &wideCharString[0], wideCharLength );
    return wideCharString;
  }

  inline std::string WideToUtf8( const std::wstring & wideCharString )
  {
    int size_needed = WideCharToMultiByte( CP_UTF8, 0, wideCharString.c_str(), -1, NULL, 0, NULL, NULL );
    std::string result( size_needed, 0 );
    WideCharToMultiByte( CP_UTF8, 0, wideCharString.c_str(), -1, &result[0], size_needed, NULL, NULL );
    return result;
  }

  inline std::string Utf8ToCp1251( const std::string & utf8String )
  {
    std::wstring wideCharString = Utf8ToWide( utf8String );
    int win1251Length = WideCharToMultiByte( 1251, 0, &wideCharString[0], -1, NULL, 0, NULL, NULL );
    std::string win1251String;
    win1251String.resize( win1251Length, ' ' );
    WideCharToMultiByte( 1251, 0, &wideCharString[0], -1, &win1251String[0], win1251Length, NULL, NULL );
    return win1251String;
  }
#else
  inline std::wstring Utf8ToWide( const std::string & utf8String )
  {
    iconv_t cd = iconv_open( "WCHAR_T", "UTF-8" );
    if ( cd == (iconv_t)-1 )
      return std::wstring( utf8String.begin(), utf8String.end() );
    const char * in = utf8String.c_str();
    size_t inLeft = utf8String.size();
    size_t inLeft0 = inLeft;
    std::wstring out( inLeft0, L'\0' );
    wchar_t * pout = &out[0];
    size_t outLeft = inLeft0 * sizeof( wchar_t );
    size_t res = iconv( cd, const_cast<char**>( &in ), &inLeft, reinterpret_cast<char**>( &pout ), &outLeft );
    iconv_close( cd );
    if ( res == (size_t)-1 )
      return std::wstring( utf8String.begin(), utf8String.end() );
    out.resize( ( inLeft0 * sizeof( wchar_t ) - outLeft ) / sizeof( wchar_t ) );
    return out;
  }

  inline std::string WideToUtf8( const std::wstring & wideCharString )
  {
    iconv_t cd = iconv_open( "UTF-8", "WCHAR_T" );
    if ( cd == (iconv_t)-1 )
    {
      std::string out;
      for ( size_t i = 0; i < wideCharString.size(); ++i )
      {
        wchar_t c = wideCharString[i];
        if ( c < 0x80 ) out += ( char )c;
        else if ( c < 0x800 ) { out += ( char )( 0xC0 | ( c >> 6 ) ); out += ( char )( 0x80 | ( c & 0x3F ) ); }
        else { out += ( char )( 0xE0 | ( c >> 12 ) ); out += ( char )( 0x80 | ( ( c >> 6 ) & 0x3F ) ); out += ( char )( 0x80 | ( c & 0x3F ) ); }
      }
      return out;
    }
    std::wstring ws( wideCharString );
    std::string out( ws.size() * 4 + 4, 0 );
    const char * inPtr = reinterpret_cast<const char*>( &ws[0] );
    size_t inLeft = ws.size() * sizeof( wchar_t );
    char * outBuf = &out[0];
    size_t outLeft = out.size();
    size_t res = iconv( cd, const_cast<char**>( &inPtr ), &inLeft, &outBuf, &outLeft );
    iconv_close( cd );
    if ( res == (size_t)-1 )
      return std::string();
    out.resize( out.size() - outLeft );
    return out;
  }

  inline std::string Utf8ToCp1251( const std::string & utf8String )
  {
    iconv_t cd = iconv_open( "CP1251", "UTF-8" );
    if ( cd == (iconv_t)-1 )
      return utf8String;
    char * in = const_cast<char*>( utf8String.c_str() );
    size_t inLeft = utf8String.size();
    std::string out( utf8String.size() * 2, 0 );
    char * outBuf = &out[0];
    size_t outLeft = out.size();
    size_t res = iconv( cd, &in, &inLeft, &outBuf, &outLeft );
    iconv_close( cd );
    if ( res == (size_t)-1 )
      return utf8String;
    out.resize( out.size() - outLeft );
    return out;
  }
#endif

  inline std::string WideToCp1251( const std::wstring & wideCharString )
  {
    return Utf8ToCp1251( WideToUtf8( wideCharString ) );
  }


  // ------------------------------------------------------------------- json
  inline Json::Value ParseJson( const char * json )
  {
    Json::Reader jsonReader;
    Json::Value root;
    const bool isOk = jsonReader.parse( json, root, false );
    return isOk ? root : Json::Value();
  }

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
  // The talent persistent name is looked up in talentsMap[] (negative ids address
  // the default class talent set, ConvertFromClassID), the same way the client
  // used to do it before the data moved server-side.
  // refineRate and the "active ability" check need the talent DB, which the lobby
  // doesn't have: they are resolved by the client (see HeroSpawn.cpp).
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

    const int levels = TALENT_LEVELS;
    const int slots  = TALENT_SLOTS;

    for ( int level = 0; level < levels; ++level )
    {
      for ( int slot = 0; slot < slots; ++slot )
      {
        // The web build is stored bottom-up relative to the in-game grid
        // (same index math the client used before).
        const unsigned tIndex  = ( unsigned )( level * slots + slot + 1 );
        const unsigned tIndex2 = ( unsigned )( ( levels - 1 - level ) * slots + slot );

        const int webTalentId = p.build[tIndex2];
        if ( webTalentId == 0 )
          continue;   // empty slot

        std::string talentName;
        if ( webTalentId > 0 )
        {
          const int nameIdx = webTalentId - 1;
          const int namesCount = ( int )( sizeof( talentsMap ) / sizeof( talentsMap[0] ) );
          if ( nameIdx < 0 || nameIdx >= namesCount )
            continue;
          talentName = talentsMap[nameIdx];
        }
        else
        {
          talentName = ConvertFromClassID( -webTalentId );
        }

        NCore::TalentInfo ti;
        ti.id           = Crc32Checksum().AddString( talentName.c_str() ).Get();
        ti.refineRate   = 0;                                    // resolved from the DB by the client
        ti.actionBarIdx = panelSlot[tIndex2];                   // -1 = not on the panel (client resolves)
        ti.isInstaCast  = smartCast[tIndex2];

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
