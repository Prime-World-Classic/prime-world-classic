#pragma once
// Self-contained SHA-256 (FIPS 180-4), no external dependencies
// (MSVC 2008 / GCC).
//
// Used by newlogin to verify web-session player keys locally, the same way
// the back-end computes them (objects/sessionStore.js):
//   playerKey = hex( sha256( str(player_id) + sessionToken + keyApi ) )
//
// C++03 on purpose: this header is shared with the VS2008 server build.

#include <cstring>

namespace WebSha256
{

namespace
{

  const unsigned int K[64] =
  {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
  };

  inline unsigned int RotR( unsigned int x, unsigned int n )
  {
    return ( x >> n ) | ( x << ( 32 - n ) );
  }

} //anonymous namespace


class Digest
{
public:
  Digest()
  {
    Reset();
  }

  void Reset()
  {
    h[0] = 0x6a09e667; h[1] = 0xbb67ae85; h[2] = 0x3c6ef372; h[3] = 0xa54ff53a;
    h[4] = 0x510e527f; h[5] = 0x9b05688c; h[6] = 0x1f83d9ab; h[7] = 0x5be0cd19;
    byteCount = 0;
    bufferLen = 0;
  }

  void AddData( const void * data, unsigned size )
  {
    const unsigned char * p = (const unsigned char *) data;
    byteCount += size;
    while ( size > 0 )
    {
      const unsigned space = 64 - bufferLen;
      const unsigned take  = size < space ? size : space;
      memcpy( buffer + bufferLen, p, take );
      bufferLen += take;
      p += take;
      size -= take;
      if ( bufferLen == 64 )
      {
        ProcessBlock( buffer );
        bufferLen = 0;
      }
    }
  }

  void AddString( const char * s )
  {
    if ( s )
      AddData( s, (unsigned)strlen( s ) );
  }

  void AddString( const char * s, unsigned size )
  {
    if ( s )
      AddData( s, size );
  }

  // Finalizes the state and returns the 32-byte digest.
  const unsigned char * Final( unsigned char out[32] )
  {
    // The bit length is taken BEFORE the padding is added.
    const unsigned long long bits = (unsigned long long) byteCount * 8ULL;

    AddData( "\x80", 1 );
    while ( bufferLen != 56 )
      AddData( "\0", 1 );

    unsigned char lenBytes[8];
    unsigned long long value = bits;
    int i;
    for ( i = 7; i >= 0; --i )
    {
      lenBytes[i] = (unsigned char)( value & 0xFF );
      value >>= 8;
    }
    AddData( lenBytes, 8 );

    for ( i = 0; i < 8; ++i )
    {
      out[i*4+0] = (unsigned char)( h[i] >> 24 );
      out[i*4+1] = (unsigned char)( h[i] >> 16 );
      out[i*4+2] = (unsigned char)( h[i] >> 8 );
      out[i*4+3] = (unsigned char)( h[i] );
    }
    return out;
  }

private:
  void ProcessBlock( const unsigned char block[64] )
  {
    unsigned int w[64];
    int t;

    for ( t = 0; t < 16; ++t )
      w[t] = ( (unsigned int)block[t*4+0] << 24 ) | ( (unsigned int)block[t*4+1] << 16 )
           | ( (unsigned int)block[t*4+2] << 8 )  | ( (unsigned int)block[t*4+3] );
    for ( t = 16; t < 64; ++t )
    {
      const unsigned int s0 = RotR( w[t-15], 7 ) ^ RotR( w[t-15], 18 ) ^ ( w[t-15] >> 3 );
      const unsigned int s1 = RotR( w[t-2], 17 ) ^ RotR( w[t-2], 19 ) ^ ( w[t-2] >> 10 );
      w[t] = w[t-16] + s0 + w[t-7] + s1;
    }

    unsigned int a = h[0], b = h[1], c = h[2], d = h[3];
    unsigned int e = h[4], f = h[5], g = h[6], hv = h[7];

    for ( t = 0; t < 64; ++t )
    {
      const unsigned int S1  = RotR( e, 6 ) ^ RotR( e, 11 ) ^ RotR( e, 25 );
      const unsigned int ch  = ( e & f ) ^ ( ( ~e ) & g );
      const unsigned int t1  = hv + S1 + ch + K[t] + w[t];
      const unsigned int S0  = RotR( a, 2 ) ^ RotR( a, 13 ) ^ RotR( a, 22 );
      const unsigned int maj = ( a & b ) ^ ( a & c ) ^ ( b & c );
      const unsigned int t2  = S0 + maj;

      hv = g; g = f; f = e; e = d + t1;
      d = c; c = b; b = a; a = t1 + t2;
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hv;
  }

  unsigned int       h[8];
  unsigned long long byteCount;
  unsigned           bufferLen;
  unsigned char      buffer[64];
};


// 32-byte digest -> 65-byte lowercase hex string (NUL-terminated).
inline void ToHex( const unsigned char digest[32], char out[65] )
{
  static const char hexChars[] = "0123456789abcdef";
  int i;
  for ( i = 0; i < 32; ++i )
  {
    out[i*2+0] = hexChars[digest[i] >> 4];
    out[i*2+1] = hexChars[digest[i] & 0x0F];
  }
  out[64] = 0;
}

} //namespace WebSha256
