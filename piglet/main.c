/*
 *  main.c
 *
 * piglet - a slippy map implementation on OpenGL ES
 *   Initially for the Raspberry Pi
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "tileTexture.h"
#include "graphics.h"

#define TILE_PNG_ROOT "/home/pi/src/charts/data"

typedef struct
{
  float longitude; // degrees E/W, -180...180
  float latitude;  // degrees N/S  -90...90
} sLatLong, *LatLong;

/*
 * Local function prototypes 
 */
static void lolaToTile( const LatLong latLong, TileCoordinate tileCoord );

/*
 * Start of code
 */
int main( int argc, char **argv )
{
  // sLatLong boatPosition = { 18.05833333, 59.33972222 };
  sLatLong boatPosition = { 17.998, 59.03685 };
  sTileCoordinate tileCoordinate;
  float zoomLevel = 11.99;

  if( argc > 1 )
    zoomLevel = atoi( argv[1] );
  // tileoordinates are: {x = 2362208165, y = 3025055848}
  // 
  lolaToTile( &boatPosition, &tileCoordinate );

  printf( "Tile coordinates @ zoom level %d: %d, %d\n", (int) floor(zoomLevel),
	  tileCoordinate.x >> (32- ((int) floor(zoomLevel)) ),
	  tileCoordinate.y >> (32- ((int) floor(zoomLevel)) ));

  // 1000 tile memory cache
  tileTex_init( TILE_PNG_ROOT, 1000 );
  
  graphics_init();

  // graphics_setMap( zoomLevel, &tileCoordinate );

  printf( "Drawing\n" );

#if 1  
  int zoomDir = 1;
  float zoomAmount = 0.001;
  while(true )
  {
#endif
    graphics_setMap( zoomLevel, &tileCoordinate );
    graphics_redraw( zoomLevel, 0, 0, 0, 0 );
#if 1
    if( zoomLevel >= 17 )
      zoomDir = -1;
    else if( zoomLevel <= 7 )
      zoomDir = 1;

    zoomLevel *= (1.0 + zoomDir * zoomAmount);

    printf( "zoom level=%f\n", zoomLevel );
    // sleep(1);
  }
#endif
  printf( "Press any key to quit" );
  getchar();

  // graphics_cleanup
  return 0;
}

/* 
   Spherical Mercator projection
*/
static void lolaToTile( const LatLong latLong, TileCoordinate tileCoord )
{
  double latRadians, mercatorRadians;
  double pow32;

  // optimize: precalculate
  pow32 = pow( 2, 32 );
  
  tileCoord->x = round((latLong->longitude + 180) * pow32 / 360.0);

  latRadians = latLong->latitude * M_PI / 180;
  mercatorRadians = log( tan( latRadians / 2.0 + M_PI / 4.0 ) );
  
  tileCoord->y = round((mercatorRadians / M_PI + 1) * (pow32 / 2));
}
