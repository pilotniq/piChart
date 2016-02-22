/*
   tileTexture.c

   Use:
     user calls:
       tile = tileTex_get( z, x, y );
       tileTex_setVisible( tile );

     then
       tileTex_waitVisibleLoaded();

     for each tile to be drawn.
       Then loops over tiles
         gets texture ID, u, v from tile.
           if no texture ID, but texture, bind texture, set texture id

       loads tile.
   API:
     -init( tilePath, inMemoryTextureCount ):
     -deinit(); // frees data, stops thread
     -requestTileBlocking( z, x, y );
     -GLuint getTileTexture( tile );  // blocking
     -setTileVisible( tile, true/false );
     -visibleTileLoaded callback
     - 
*/

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>

#include <png.h>

#include <voxi/util/hash.h>
#include <voxi/util/threading.h>

#include "tileTexture.h"

typedef struct
{
  char *buffer;
  size_t remainingBytes;
} sMemPNG, *MemPNG;

typedef struct sTileTexture
{
  int z;

  uint32_t x, y;

  TileTexType type;
  struct sTileTexture *above;
  struct sTileTexture *below[4];

  /* These may only be changed while queue mutex is locked */
  struct sTileTexture *nextInQueue;  // in load queue if not loaded, else next in unload queue
  struct sTileTexture *prevInQueue;  // in load queue if not loaded, else next in unload queue
  
  int visibleRefsCount; // number of visible subtextures that reference this one

  union
  {
    struct
    {
      struct sTileTexture *otherTile;
      float u1, v1, u2, v2;
    } otherTile;

    struct
    {
      GLuint textureID;
      bool inRAM;
      sMemPNG pngData;
    } loadedTile;
  } typeData;

  // Statusl
  bool visible;
  
} sTileTexture;

/*
 * static functions
 */
static void *threadFunc( void *arg );
static int tileHash( TileTexture t );
static int tileCompare( TileTexture t1, TileTexture t2 );
static TileTexture tileCreate( int z, uint32_t x, uint32_t y );
static void png_memoryReadFunc( png_structp png_ptr, png_bytep outBytes,
				png_size_t byteCountToRead );
static void loadTile( TileTexture tile );
static Error loadPngFromMemory( const sMemPNG *pngData, GLubyte **image,
				int *outWidth, int *outHeight );
static void printLoadQueue( const sTileTexture *head );
static void validateLoadQueues();
static void findRefTile( TileTexture tile );

/*
 * Global data
 */

// static TileTexture unloadQueue;
static int inMemoryCount;
static char *tilePath;

static pthread_t thread;

static sVoxiMutex loadQueueMutex;
static pthread_cond_t loadQueueCondition;
static TileTexture visibleLoadQueueHead, visibleLoadQueueTail;
static TileTexture invisibleLoadQueueHead, invisibleLoadQueueTail;

static HashTable tileHashTable;

Error tileTex_init( const char *tilePathParam, int inMemoryCountParam )
{
  int err;
  Error error;
  
  tilePath = strdup( tilePathParam );
  assert( tilePath != NULL );

  visibleLoadQueueHead = NULL;
  visibleLoadQueueTail = NULL;
  invisibleLoadQueueHead = NULL;
  invisibleLoadQueueTail = NULL;
  
  inMemoryCount = inMemoryCountParam;
  tileHashTable = HashCreateTable( 129, (HashFuncPtr) tileHash,
				   (CompFuncPtr) tileCompare, NULL );
  
  ERR_GOTO( threading_init(), FAIL );

  ERR_GOTO( threading_mutex_init( &loadQueueMutex ), FAIL );
  threading_mutex_setDebug( &loadQueueMutex, false );

  err = pthread_cond_init( &loadQueueCondition, NULL );
  assert( err == 0 );
  
  // create thread
  err = pthread_create( &thread, NULL /* attrs */, threadFunc, NULL );
  assert( err == 0 );

  return NULL;

 FAIL:
  return error; // should wrap it.
}

static int tileCompare( TileTexture t1, TileTexture t2 )
{
  if( t1->z != t2->z )
    return t2->z - t1->z;

  if( t1->x != t2->x )
    return t2->x - t1->x;

  return t2->y - t1->y;
}

static int tileHash( TileTexture t )
{
  return t->z ^ (t->x << 1) ^ (t->y << 2);
}

void tileTex_setVisible( TileTexture tile, bool isVisible )
{
  const char *oldLocker;
  if( tile->visible == isVisible )
    return;
  /*
  printf( "tileTex_setVisible( %p: %d, %d, %d )\n", tile, tile->z, tile->x,
	  tile->y );
  */
  // remove tile from old queue //
  // how do we know if it was in the invisible queue? It may have been visible,
  // then turned inviisble.
  // only if it is of type TILE_NO_DATA, do this.
  if( tile->type == TILE_NEW )
  {
    // lock the Load Queue Mutex
    oldLocker = threading_mutex_lock_debug( &loadQueueMutex, "tileTex_setVisible" );
    validateLoadQueues();

    // printf( "tileTex_setVisible( (%d, %d, %d), %s )\n", tile->z, tile->x, tile->y, isVisible ? "true": "false" );

    if( tile->visible )
    {
      if( tile->prevInQueue == NULL )
	visibleLoadQueueHead = tile->nextInQueue;
      else
	tile->prevInQueue->nextInQueue = tile->nextInQueue;
      
      if( tile->nextInQueue == NULL )
	visibleLoadQueueTail = tile->prevInQueue;
      else
	tile->nextInQueue->prevInQueue = tile->prevInQueue;
    }
    else
    {
      if( tile->prevInQueue == NULL )
      {
	invisibleLoadQueueHead = tile->nextInQueue;
	// printf( "tileTex_setVisible: invisibleLoadQUeueHead set to %p\n", tile->nextInQueue );
      }
      else
	tile->prevInQueue->nextInQueue = tile->nextInQueue;

      if( tile->nextInQueue == NULL )
	invisibleLoadQueueTail = tile->prevInQueue;
      else
	tile->nextInQueue->prevInQueue = tile->prevInQueue;
    }
    tile->nextInQueue = NULL;
    tile->prevInQueue = NULL;
  }
  else
  {
    // reinstate these, check why this triggers
    assert( tile->nextInQueue == NULL );
    assert( tile->prevInQueue == NULL );
  }
  
  tile->visible = isVisible;

  if( tile->type == TILE_NEW )
  {
    // insert into new queue
    if( tile->visible )
    {
      if( visibleLoadQueueTail == NULL )
	visibleLoadQueueHead = visibleLoadQueueTail = tile;
      else
      {
	visibleLoadQueueTail->nextInQueue = tile;
	tile->prevInQueue = visibleLoadQueueTail;
	visibleLoadQueueTail = tile;
      }
    }
    else
    {
      if( invisibleLoadQueueTail == NULL )
      {
	invisibleLoadQueueHead = invisibleLoadQueueTail = tile;
      }
      else
      {
	invisibleLoadQueueTail->nextInQueue = tile;
	tile->prevInQueue = invisibleLoadQueueTail;
	invisibleLoadQueueTail = tile;
      }
    }
    // I don't think we need to broadcast when switching between queues
    // pthread_cond_broadcast( &loadQueueCondition );
#if 0
    printf( "setVisible: Visible Queue" );
    printLoadQueue( visibleLoadQueueHead );
    printf( "setVisible: Invisible Queue" );
    printLoadQueue( invisibleLoadQueueHead );
#endif
    validateLoadQueues();
    threading_mutex_unlock_debug( &loadQueueMutex, oldLocker );
  }
  
  if( tile->visible )
  {
    int x, y;
    
    // start loading neighboring tiles at same zoom level
    for( y = tile->y - 1; y <= (tile->y + 1); y++ )
      for( x = tile->x - 1; x <= (tile->x + 1); x++ )
	if( !((tile->x == x) && (tile->y == y) ) )
	  tileTex_get( tile->z, x, y );  // will put in inivisible load queue if not loaded
    
    // TODO: preload
    // load tile one level above
    tileTex_get( tile->z - 1, tile->x / 2, tile->y / 2 );
    
    // load tiles one level below
    tileTex_get( tile->z + 1, tile->x * 2,     tile->y * 2 );
    tileTex_get( tile->z + 1, tile->x * 2,     tile->y * 2 + 1 );
    tileTex_get( tile->z + 1, tile->x * 2 + 1, tile->y * 2 );
    tileTex_get( tile->z + 1, tile->x * 2 + 1, tile->y * 2 + 1 );
  }

}

TileTexture tileTex_get(int z, uint32_t x, uint32_t y)
{
  TileTexture tile;
  sTileTexture tileTemplate;

  tileTemplate.z = z; tileTemplate.x = x; tileTemplate.y = y;
  
  // if the tile has been created, find it
  tile = (TileTexture) HashFind( tileHashTable, &tileTemplate );
		  
  // if the tile has not been created, create it
  if( tile == NULL )
    tile = tileCreate( z, x, y );
  
  // return it

  return tile;
}

static TileTexture tileCreate( int z, uint32_t x, uint32_t y )
{
  TileTexture tile;
  const char *oldLocker;
  
  tile = malloc( sizeof( sTileTexture ) );
  assert( tile != NULL );

  tile->x = x;
  tile->y = y;
  tile->z = z;

  tile->type = TILE_NEW;
  tile->above = NULL; // when to we get this
  tile->below[0] = NULL;
  tile->below[1] = NULL;
  tile->below[2] = NULL;
  tile->below[3] = NULL;

  tile->nextInQueue = NULL;
  tile->visible = false;
  tile->visibleRefsCount = 0;

  HashAdd( tileHashTable, tile );
  
  /* put it in theload queue? Yes! */
  oldLocker = threading_mutex_lock_debug( &loadQueueMutex, "tileCreate" );
  validateLoadQueues();
    
  if( invisibleLoadQueueHead == NULL )
  {
    assert( invisibleLoadQueueTail == NULL );
    invisibleLoadQueueHead = tile;
    // printf( "tileCreate: invisibleLoadQUeueHead set to %p\n", tile->nextInQueue );
  }
  else
    invisibleLoadQueueTail->nextInQueue = tile;
  
  tile->prevInQueue = invisibleLoadQueueTail;
  invisibleLoadQueueTail = tile;

  // printf( "TileCreate: signaling loadQueueCondition\n" );
  pthread_cond_signal( &loadQueueCondition );

  validateLoadQueues();
  threading_mutex_unlock_debug( &loadQueueMutex, oldLocker );
  
  return tile;
}

static void *threadFunc( void *arg )
{
  TileTexture tileToLoad;
  Error error;
  const char *oldLocker;
  
  do
  {
    // wait for load requests or shutdown
    oldLocker = threading_mutex_lock_debug( &loadQueueMutex, "threadFunc" );
    validateLoadQueues();
    
    while( (visibleLoadQueueHead == NULL) && (invisibleLoadQueueHead == NULL) )
    {
      error = threading_cond_wait( &loadQueueCondition, &loadQueueMutex );

      // printf( "threadFunc got signal, visible head=%p, invisible head=%p\n",
      //        visibleLoadQueueHead, invisibleLoadQueueHead );
      
      assert( error == NULL );
    }
    
    if( visibleLoadQueueHead != NULL )
    {
      tileToLoad = visibleLoadQueueHead;
      /*      
      visibleLoadQueueHead = tileToLoad->nextInQueue;
      if( visibleLoadQueueHead == NULL )
      {
	assert( visibleLoadQueueTail == tileToLoad );
	visibleLoadQueueTail = NULL;
      }
      */ // Tile removed from queue in loadTile?
    }
    else
    {
      tileToLoad = invisibleLoadQueueHead;
      /*
      invisibleLoadQueueHead = tileToLoad->nextInQueue;
      if( invisibleLoadQueueHead == NULL )
      {
	assert( invisibleLoadQueueTail == tileToLoad );
	invisibleLoadQueueTail = NULL;
      }
      */
    }
    
    assert( tileToLoad != NULL );
    
    // loadQueue = tileToLoad->nextInQueue;
    assert( tileToLoad->prevInQueue == NULL );
    // tileToLoad->nextInQueue = NULL;
    
    validateLoadQueues();
    threading_mutex_unlock_debug( &loadQueueMutex, oldLocker );
  
    // load the tile

    loadTile( tileToLoad );
  } while( true );  // TODO: abort when tileTexture is shut down
}

// called from the loading thread
static void findRefTile( TileTexture tile )
{
  int x, y, z;
  TileTexture upTile, refTile;
  double u1, v1, uvScale;

  // printf( "findRefTile( %p: %d, %d, %d )\n", tile, tile->z, tile->x, tile->y );
  
  if( tile->z == 0 )
  {
    tile->type = TILE_NO_DATA;
    return;
  }
  
  x = tile->x >> 1;
  y = tile->y >> 1;
  z = tile->z - 1;

  upTile = tileTex_get( z, x, y );
  if( upTile->type == TILE_NEW ) // not loaded yet; load now
    loadTile( upTile );
    
  switch( upTile->type )
  {
    case TILE_HAS_TEXTURE:
      // got it!
      // printf( "***\n" );
      tile->type = TILE_REFS_TEXTURE;
      refTile = upTile;
      tile->typeData.otherTile.otherTile = upTile;

      u1 = (tile->x & 1) / 2.0;
      v1 = (~(tile->y) & 1) / 2.0;
      
      tile->typeData.otherTile.u1 = u1;
      tile->typeData.otherTile.u2 = u1 + 0.5;
      tile->typeData.otherTile.v1 = v1;
      tile->typeData.otherTile.v2 = v1 + 0.5;
      break;

    case TILE_REFS_TEXTURE:
      tile->type = TILE_REFS_TEXTURE;
      refTile = upTile->typeData.otherTile.otherTile;
      tile->typeData.otherTile.otherTile = refTile;

      // mask = (1 << (steps - 1)) - 1;
      // mask = ~((uint32_t) 0) >> (32-steps);

      // one step up, scale is 0.5
      // new u = upTile->u + (tile->x & 1) * uvScale * 0.5;
      uvScale = 1.0 / (1 << (tile->z - refTile->z));

      u1 = upTile->typeData.otherTile.u1 + (tile->x & 1) * uvScale * 0.5;
      v1 = upTile->typeData.otherTile.v1 + (tile->y & 1) * uvScale * 0.5;

      tile->typeData.otherTile.u1 = u1;
      tile->typeData.otherTile.u2 = u1 + uvScale;
      tile->typeData.otherTile.v1 = v1;
      tile->typeData.otherTile.v2 = v1 + uvScale;
      break;

    case TILE_NO_DATA:
      // no data for the parent
      tile->type = TILE_NO_DATA;
      break;

    case TILE_NEW:
      assert( false );
  } // endo of switch
#if 0
  if( tile->type == TILE_NO_DATA )
    printf( "findRefTile got no data\n" );
  else
    printf( "findRefTile of (%p: %d, %d, %d) is (%p: %d, %d, %d; u(%f, %f), v(%f %f)\n",
	    tile, tile->z, tile->x, tile->y, refTile, refTile->z, refTile->x, refTile->y,
	    tile->typeData.otherTile.u1, tile->typeData.otherTile.u2,
	    tile->typeData.otherTile.v1, tile->typeData.otherTile.v2 );
#endif
}
     
static void loadTile( TileTexture tile )
{
  char filename[ 256 ];
  FILE *file;
  long size;
  size_t itemCount;

  sprintf( filename, "%s/%02d/%d/%d.png", tilePath, tile->z, tile->x, tile->y );
#if 0
  printf( "loading tile %p: %d, %d, %d: '%s'\n", tile, tile->z, tile->x, tile->y,
	  filename );
  printf( "VisibieLoadQueue: " );
  printLoadQueue( visibleLoadQueueHead);
  printf( "InvisibieLoadQueue: " );
  printLoadQueue( invisibleLoadQueueHead);
#endif
  // get the size of the file
  file = fopen( filename, "r" );
  if( file == NULL )
  {
    printf( "failed to load '%s'\n", filename );
    // tile->type = TILE_REFS_TEXTURE;

    // Look for the tile higher up in the zoom hierarchy (recursively)
    // reference that tile, and calculate u,v
    findRefTile( tile );
    
    // must remove it from the load queue for now
    
    // TODO
    goto DONE;
    // return;
    // assert( false ); // NIY
  }

  fseek( file, 0, SEEK_END); // seek to end of file
  size = ftell( file ); // get current file pointer
  fseek( file, 0, SEEK_SET); // seek back to beginning of file
  
  // proceed with allocating memory and reading the file
  tile->typeData.loadedTile.pngData.buffer = malloc( size );
  assert( tile->typeData.loadedTile.pngData.buffer != NULL );
  tile->typeData.loadedTile.pngData.remainingBytes = size;

  itemCount = fread( tile->typeData.loadedTile.pngData.buffer, size, 1, file );
  assert( itemCount == 1 );

  fclose( file );

  tile->typeData.loadedTile.textureID = -1;
  tile->typeData.loadedTile.inRAM = true;
  tile->type = TILE_HAS_TEXTURE;

 DONE:
  // remove from load queue
  threading_mutex_lock( &loadQueueMutex );
  validateLoadQueues();

  if( tile->visible )
  {
    visibleLoadQueueHead = tile->nextInQueue;
    
    if( visibleLoadQueueHead == NULL )
      visibleLoadQueueTail = NULL;
    else
      visibleLoadQueueHead->prevInQueue = NULL;
  }
  else
  {
    assert( invisibleLoadQueueHead != NULL ); // this triggers

    if( tile->prevInQueue == NULL )
      invisibleLoadQueueHead = tile->nextInQueue;
    else
      tile->prevInQueue->nextInQueue = tile->nextInQueue;

    if( tile->nextInQueue == NULL )
      invisibleLoadQueueTail = tile->prevInQueue;
    else
      tile->nextInQueue->prevInQueue = tile->prevInQueue;

    // printf( "loadTile: invisibleLoadQueueHead set to %p\n", invisibleLoadQueueHead );
#if 0
    if( invisibleLoadQueueHead == NULL )
      invisibleLoadQueueTail = NULL;
    else
      invisibleLoadQueueHead->prevInQueue = NULL;
#endif
    assert( ((invisibleLoadQueueHead == NULL) && (invisibleLoadQueueTail == NULL) ) ||
	    ((invisibleLoadQueueHead != NULL) && (invisibleLoadQueueTail != NULL) ) );
  }
  tile->nextInQueue = NULL;
  tile->prevInQueue = NULL;
  
  // someone may be waiting for the queue to be empty
  pthread_cond_broadcast( &loadQueueCondition );
#if 0
  printf( "LoadTile exit: VisibieLoadQueue: " );
  printLoadQueue( visibleLoadQueueHead );
  printf( "invisibieLoadQueue: " );
  printLoadQueue( invisibleLoadQueueHead );
#endif
  validateLoadQueues();
  threading_mutex_unlock( &loadQueueMutex );
}

/* Returns texture ID */
GLuint tileTex_makeTextureID( TileTexture tile )
{
  GLubyte *imageBuffer;
  Error error;
  int width, height; // texture width, height
  
  switch( tile->type )
  {
    case TILE_NO_DATA:
      // printf( "tile has no data, don't make texture ID.\n" );
      return -1;

    case TILE_REFS_TEXTURE:
      // return tileTex_makeTextureID( tile->typeData.otherTile.otherTile );
      return -1;
      
    case TILE_HAS_TEXTURE:
      if( tile->typeData.loadedTile.textureID != -1 )
	return tile->typeData.loadedTile.textureID;

      // generate texture ID
      glGenTextures( 1, &(tile->typeData.loadedTile.textureID) );
      assert( glGetError() == GL_NO_ERROR );

      glBindTexture(GL_TEXTURE_2D, tile->typeData.loadedTile.textureID ); // texture_map[ i ]);
      assert( glGetError() == GL_NO_ERROR );

      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, /* GL_NEAREST */ GL_LINEAR );
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, /* GL_NEAREST */ GL_LINEAR );
      assert( glGetError() == GL_NO_ERROR );

      // decompress PNG. Would be interesting to profile how long this call takes.
      error = loadPngFromMemory( &(tile->typeData.loadedTile.pngData),
				 &imageBuffer, &width, &height );
      assert( error == NULL );
      
      glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB,
			GL_UNSIGNED_BYTE, imageBuffer );
      assert( glGetError() == GL_NO_ERROR );

      free( imageBuffer );

      return tile->typeData.loadedTile.textureID;
      break;

  default:
    assert( false );
  }
}

static Error loadPngFromMemory( const sMemPNG *pngData, GLubyte **outData,
				int *outWidth, int *outHeight )
{
  png_structp png_ptr;
  png_infop info_ptr;
  sMemPNG memPNG;

  assert( pngData != NULL );
  memPNG = *pngData;

  /* Create and initialize the png_struct
   * with the desired error handler
   * functions.  If you want to use the
   * default stderr and longjump method,
   * you can supply NULL for the last
   * three parameters.  We also supply the
   * the compiler header file version, so
   * that we know if the application
   * was compiled with a compatible version
   * of the library.  REQUIRED
   */
  png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
				   NULL, NULL, NULL);

  if (png_ptr == NULL)
    return ErrNew( ERR_APP, 0, NULL, "png_create_read_struct failed" );

  /* Allocate/initialize the memory
   * for image information.  REQUIRED. */
  info_ptr = png_create_info_struct(png_ptr);
  if (info_ptr == NULL)
    return ErrNew( ERR_APP, 0, NULL, "png_create_info_struct failed" );

  /* Set error handling if you are
   * using the setjmp/longjmp method
   * (this is the normal method of
   * doing things with libpng).
   * REQUIRED unless you  set up
   * your own error handlers in
   * the png_create_read_struct()
   * earlier.
   */
  if (setjmp(png_jmpbuf(png_ptr))) {
    /* Free all of the memory associated
     * with the png_ptr and info_ptr */
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    /* If we get here, we had a
     * problem reading the file */
    return ErrNew( ERR_APP, 0, NULL, "png_jmbuf problem" );
  }

  /* Set up the output control if
   * you are using standard C streams */
  // png_init_io(png_ptr, (png_FILE_p) &memPNG );
  png_set_read_fn( png_ptr, (png_rw_ptr) &memPNG,
		   (png_rw_ptr) png_memoryReadFunc );

  /* If we have already
   * read some of the signature */
  // png_set_sig_bytes(png_ptr, sig_read);

  /*
   * If you have enough memory to read
   * in the entire image at once, and
   * you need to specify only
   * transforms that can be controlled
   * with one of the PNG_TRANSFORM_*
   * bits (this presently excludes
   * dithering, filling, setting
   * background, and doing gamma
   * adjustment), then you can read the
   * entire image (including pixels)
   * into the info structure with this
   * call
   *
   * PNG_TRANSFORM_STRIP_16 |
   * PNG_TRANSFORM_PACKING  forces 8 bit
   * PNG_TRANSFORM_EXPAND forces to
   *  expand a palette into RGB
   */
  png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_STRIP_16 |
	       PNG_TRANSFORM_PACKING | PNG_TRANSFORM_EXPAND, NULL);

  png_uint_32 width, height;
  int bit_depth;
  int color_type, interlace_type;

  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
	       &interlace_type, NULL, NULL);
  if( outWidth != NULL )
    *outWidth = width;
  if( outHeight != NULL )
    *outHeight = height;

  unsigned int row_bytes = png_get_rowbytes(png_ptr, info_ptr);
  *outData = (unsigned char*) malloc(row_bytes * height);

  png_bytepp row_pointers = png_get_rows(png_ptr, info_ptr);

  int i;
  for (i = 0; i < height; i++) {
    // note that png is ordered top to
    // bottom, but OpenGL expect it bottom to top
    // so the order or swapped
    // memcpy(*outData+(row_bytes * (height-1-i)), row_pointers[i], row_bytes);
    memcpy(*outData+(row_bytes * i), row_pointers[i], row_bytes);
  }

  /* Clean up after the read,
   * and free any memory allocated */
  png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

  /* That's it */
  return NULL;  // no error
}

static void png_memoryReadFunc( png_structp png_ptr, png_bytep outBytes,
				png_size_t byteCountToRead )
{
  MemPNG memPNG = (MemPNG) png_get_io_ptr(png_ptr);
  size_t actualReadCount;
  
  if( memPNG == NULL)
    return;   // add custom error handling here

  if( byteCountToRead > memPNG->remainingBytes )
    actualReadCount = memPNG->remainingBytes;
  else
    actualReadCount = byteCountToRead;
  
  // TODO: check that we don't read past our memory buffer
  // would be nice if we could do this without copy
  memcpy( outBytes, memPNG->buffer, actualReadCount );

  memPNG->remainingBytes -= actualReadCount;
  memPNG->buffer += actualReadCount;
  
#if 0
  if((png_size_t)bytesRead != byteCount)
    return;   // add custom error handling here
#endif
}

void tileTex_waitVisibleLoaded()
{
  threading_mutex_lock( &loadQueueMutex );
  validateLoadQueues();

  // printf( "waitVisibleLoaded entry: " );
  // printVisibleLoadQueue();
  
  while( visibleLoadQueueHead != NULL )
  {
    Error error;
    
    error = threading_cond_wait( &loadQueueCondition, &loadQueueMutex );
    assert( error == NULL );
  }

  threading_mutex_unlock( &loadQueueMutex );
}

static void printLoadQueue( const sTileTexture *head )
{
  const sTileTexture *tt;

  printf( "Queue: " );
  
  for( tt = head; tt != NULL; tt = tt->nextInQueue )
    printf( "(%p: %d,%d,%d) ", tt, tt->z, tt->x, tt->y );

  printf( "\n" );
}

static void validateLoadQueues()
{
  assert( ((visibleLoadQueueHead == NULL) && (visibleLoadQueueTail == NULL) ) ||
	  ((visibleLoadQueueHead != NULL) && (visibleLoadQueueTail != NULL) ));
  // triggers on long run to top.
  assert( ((invisibleLoadQueueHead == NULL) && (invisibleLoadQueueTail == NULL) ) ||
  	  ((invisibleLoadQueueHead != NULL) && (invisibleLoadQueueTail != NULL) ));

  if( visibleLoadQueueHead != NULL )
  {
    assert( visibleLoadQueueHead->prevInQueue == NULL );
    assert( visibleLoadQueueTail->nextInQueue == NULL );
    assert( visibleLoadQueueHead->visible );
    assert( visibleLoadQueueTail->visible );
  }

  if( invisibleLoadQueueHead != NULL )
  {
    assert( invisibleLoadQueueHead->prevInQueue == NULL );
    assert( invisibleLoadQueueTail->nextInQueue == NULL );
    assert( !invisibleLoadQueueHead->visible );
    assert( !invisibleLoadQueueTail->visible ); // this triggers on zooming demo, check why TODO
  }

}

void tileTex_getUV( TileTexture tile, float *u0, float *v0, float *u1, float *v1 )
{
  switch( tile->type )
  {
    case TILE_HAS_TEXTURE:
    case TILE_NO_DATA:
    case TILE_NEW:
      *u0 = *v0 = 0.0f;
      *u1 = *v1 = 1.0f;
      break;

    case TILE_REFS_TEXTURE:
      *u0 = tile->typeData.otherTile.u1;
      *u1 = tile->typeData.otherTile.u2;
      *v0 = tile->typeData.otherTile.v1;
      *v1 = tile->typeData.otherTile.v2;
      break;

    default:
      assert( FALSE );
  }
}
