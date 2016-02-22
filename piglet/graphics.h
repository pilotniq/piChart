/*
 * graphics.h
 *
 * OpenGL ES implementation of graphics layer for "Slippymap" application
 *
 */

#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>

/* Tile coordinates are integers, where the highest order bit indicates the 
   coordinate at zoom level 1, the first two bits the coordinates at zoom 
   level 2 etc.
*/
typedef struct
{
  uint32_t x, y;
} sTileCoordinate, *TileCoordinate;

void graphics_init();
void graphics_setMap( float scale, const TileCoordinate center );
void graphics_redraw( float zoom, uint32_t top, uint32_t bottom, uint32_t left,
		      uint32_t right );

#endif
