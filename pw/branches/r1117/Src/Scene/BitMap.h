#pragma once

#include "../System/BitData.h"
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
namespace NScene
{

class BitMap
{
  ZDATA
		CArray2D1Bit mask;
		CVec2 mult;
public:
  ZEND int operator&( IBinSaver &f ) { f.Add(2,&mask); f.Add(3,&mult); return 0; }

private:
	int GetX( const float x ) const { return Clamp( (int)floor( x * mult.x ), 0, mask.GetSizeX() ); }
	int GetY( const float y ) const { return Clamp( (int)floor( y * mult.y ), 0, mask.GetSizeY() ); }
	BitMap( const char* data, float mapSizeX, float mapSizeY, int maskSize );

public:
  BitMap() : mult( VNULL2 ) {}

  BitMap( bool water, const CVec2& mapSize )
  {
    mask.SetSizes( 1, 1 );

    if ( water )
      mask.Set1();
    else
      mask.FillZero();

    mult.x = 1.0f/mapSize.x;
    mult.y = 1.0f/mapSize.y;
  }

  bool CheckPos( const CVec2& pos ) const
  {
		NI_VERIFY( !mask.IsEmpty(), "BitMap not initialized", return false );
		return mask.GetData( GetX( pos.x ), GetY( pos.y ) ) > 0;
  }

  // Grow the masked area by `dist` world units in every direction (square brush, two separable sliding-window passes).
  // Used by the world logic copy of the water mask: the painted mask covers the deep part of the river only.
  void DilateWorld( float dist )
  {
    if ( mask.IsEmpty() || dist <= 0.0f )
      return;
    const int r = (int)ceil( dist * Max( mult.x, mult.y ) );
    const int sx = mask.GetSizeX(), sy = mask.GetSizeY();
    CArray2D1Bit rows( sx, sy );
    rows.FillZero();
    for ( int y = 0; y < sy; ++y )
    {
      int count = 0;
      for ( int x = 0; x < r && x < sx; ++x )
        count += mask.GetData( x, y );
      for ( int x = 0; x < sx; ++x )
      {
        if ( x + r < sx ) count += mask.GetData( x + r, y );
        if ( x - r - 1 >= 0 ) count -= mask.GetData( x - r - 1, y );
        if ( count > 0 ) rows.SetData( x, y );
      }
    }
    mask.FillZero();
    for ( int x = 0; x < sx; ++x )
    {
      int count = 0;
      for ( int y = 0; y < r && y < sy; ++y )
        count += rows.GetData( x, y );
      for ( int y = 0; y < sy; ++y )
      {
        if ( y + r < sy ) count += rows.GetData( x, y + r );
        if ( y - r - 1 >= 0 ) count -= rows.GetData( x, y - r - 1 );
        if ( count > 0 ) mask.SetData( x, y );
      }
    }
  }

	static void Store( const char* data, float mapSizeX, float mapSizeY, int maskSize, const string& fileName );
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
}