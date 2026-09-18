#pragma once
// ============================================================================
// Helpers for the web payloads (synchronizer / launcher JSON).
//
// The encoding conversions are moved out of PF_GameLogic/WebLauncher.h
// unchanged (Win32 API on Windows, iconv on Linux), so the byte-level behaviour
// of the nickname handling stays exactly as it was.
// ============================================================================

#include <string>

#if !defined( NV_WIN_PLATFORM )
#include <iconv.h>
#endif

#include <json/json.h>


namespace WebSession
{
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
} // namespace WebSession
