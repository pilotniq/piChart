/*
   tileTexture.h
*/

#ifndef TILE_TEXTURE_H
#define TILE_TEXTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "GLES2/gl2.h"
// #include <GL/gl.h>

#include <voxi/util/err.h>

typedef struct sTileTexture *TileTexture;

// TODO: Create enum TILE_NEW
typedef enum { TILE_NEW, TILE_HAS_TEXTURE, TILE_REFS_TEXTURE, TILE_NO_DATA } TileTexType;

Error tileTex_init( const char *tilePathParam, int inMemoryCountParam );
void tileTex_setVisible( TileTexture tile, bool isVisible );
TileTexture tileTex_get(int z, uint32_t x, uint32_t y);
GLuint tileTex_makeTextureID( TileTexture tile );
void tileTex_waitVisibleLoaded( void );
void tileTex_getUV( TileTexture tile, float *u0, float *v0, float *u1, float *v1 );

#endif
