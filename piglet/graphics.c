/*
 * graphics.c
 *
 * OpenGL ES implementation of graphics layer for "Slippymap" application
 *
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#include "bcm_host.h"

#include "GLES2/gl2.h"
// #include "GLES2/gl2.h" // for GL_UNSIGNED_INT
#include "EGL/egl.h"
#include "EGL/eglext.h"

#include "tileTexture.h"
#include "graphics.h"

/*
 * local types
 */

#define SHADER_VERTEX_INDEX 0
#define SHADER_UV_INDEX 1
#define SHADER_TID_INDEX 2

/*
 * A tile has:
 *   3 vertices with uv
 *   4 vector indexes 
 *   1 texture ID
 */
typedef struct 
{
  GLfloat /* uint */ position[2]; // TODO: Change this to float
  float  uv[2];
} sVertexData, *VertexData;

typedef struct
{
  sVertexData vertexData[4];
  GLuint vertexBufferID;
  // GLuint textureID; // get from tileTexture
  TileTexture tileTexture;
} sTileData, *TileData;

GLubyte tileIndices[ 4 ] = { 1, 0, 2, 3 };

#define MAX_TILES (4 * 5)

sTileData tiles[ MAX_TILES ];

/*
typedef
{
  GLubyte position[3];
  float uv[3][2];
  GLuint textureID; // texture id
} sTriangleData, *TriangleData;
*/

/*
 * static data 
 */
static uint32_t screenWidth;
static uint32_t screenHeight;
// OpenGL co-ordinates are tile coordinates - tileCenter
static sTileCoordinate tileCenter;
static GLuint /* vertexBufferID, */ indexBufferID;
// static GLuint textureIDs[ 15 ]; // worst case should be 15 textures
static void transformVertex( const TileData tile, const float *scaleMatrix,
			     float *transformed );

/*
    The buffers I want:

      Vertices: world positions
      Triangles: vertex index[3]
                 texture position uv[3]
                 texture id
*/
// static sVertexData vertices[ 4 * 6 ];
/* Worst case vertex count is now 4 x 6 */
// static GLuint vertices[ 4 * 6 ][3];

/* worst case triangle count is 3 x 5 x 2 = 30, with 90 vertices */
// static sTriangleData indices[ 30 ];

static int /* triangleCount, */ visibleTileCount;

/* textures in order (fix later to better structure) */
// GLubyte *textures[6][4];

// Handle to a program object
GLuint programObject;

// Attribute locations
GLint  positionLoc;
GLint  texCoordLoc;

// Uniform locations
GLint texturesUniformLoc;
GLint viewMatrixLoc; 

// Sampler location
GLint samplerLoc;

// Texture handle
// GLuint textureId;

/*
 * local global variables
 */
static EGLDisplay display;
static EGLSurface surface;
static EGLContext context;

/*
 * local functions
 */
static void init_ogl( void  );
static GLuint LoadProgram ( const char *vertShaderSrc, const char *fragShaderSrc );
static GLuint LoadShader(GLenum type, const char *shaderSrc);

/*
 * start of code 
 */
void graphics_init()
{
    const char *vShaderStr = "attribute vec4 a_position;  \n"
    "uniform mat4 u_ViewMatrix;        // A constant representing the combined model/view matrix. \n"
    "attribute vec2 a_texCoord;   \n"
    "varying vec2 v_texCoord;     \n"
    "void main()                  \n"
    "{                            \n"
    "   gl_Position = u_ViewMatrix * a_position; \n"
    "   v_texCoord = a_texCoord;  \n"
    "}                            \n";
#if 0
  const char *vShaderStr =
    "attribute vec4 a_position;  \n"
    "uniform mat4 u_ViewMatrix;        // A constant representing the combined model/view matrix. \n"
    "attribute vec2 a_texCoord;   \n"
    "varying vec2 v_texCoord;     \n"
    "void main()                  \n"
    "{                            \n"
    "   gl_Position = u_ViewMatrix * a_position; \n"
    "   v_texCoord = a_texCoord;  \n"
    "}                            \n";
#endif
  const char *fShaderStr =
         "precision mediump float;                            \n"
          "varying vec2 v_texCoord;                            \n"
          "uniform sampler2D s_texture;                        \n"
          "void main()                                         \n"
          "{                                                   \n"
          "  gl_FragColor = texture2D( s_texture, v_texCoord );\n"
          "  // gl_FragColor = vec4( 1.0, 0.0, 0.0, 1.0 );\n"
    "}                                                   \n";
  int i;
  
  init_ogl();

  assert( glGetError() == GL_NO_ERROR );

   // Set background color and clear buffers
   glClearColor(0.15f, 0.25f, 0.35f, 1.0f);

   // Load the shaders and get a linked program object
   programObject = LoadProgram( vShaderStr, fShaderStr );
   assert( glGetError() == GL_NO_ERROR );

   // Get the attribute locations
   positionLoc = glGetAttribLocation ( programObject, "a_position" );
   texCoordLoc = glGetAttribLocation ( programObject, "a_texCoord" );
   assert( glGetError() == GL_NO_ERROR );

   viewMatrixLoc = glGetUniformLocation( programObject, "u_ViewMatrix" );
   
   // Get the sampler location
   samplerLoc = glGetUniformLocation ( programObject, "s_texture" );

   // glGenBuffers( 1, &vertexBufferID );
   glGenBuffers( 1, &indexBufferID );
   assert( glGetError() == GL_NO_ERROR );

   for( i = 0; i < MAX_TILES; i++ )
     tiles[i].vertexBufferID = -1;
     
   // Enable back face culling.
   // glEnable(GL_CULL_FACE);
}

static void init_ogl( void  )
{
  int32_t success;

   static EGL_DISPMANX_WINDOW_T nativewindow;

   DISPMANX_ELEMENT_HANDLE_T dispman_element;
   DISPMANX_DISPLAY_HANDLE_T dispman_display;
   DISPMANX_UPDATE_HANDLE_T dispman_update;
   VC_RECT_T dst_rect;
   VC_RECT_T src_rect;
   EGLBoolean result;
   
   // Change this to get without alpha to save some resources.
   static const EGLint attribute_list[] =
   {
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 0, // was 8
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_NONE
   };

   static const EGLint context_attributes[] =
   {
     EGL_CONTEXT_CLIENT_VERSION, 2,
     EGL_NONE
   };
   
   EGLConfig config;
   EGLint num_config;

   bcm_host_init();
   
   // get an EGL display connection
   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   assert( display!=EGL_NO_DISPLAY );

   // initialize the EGL display connection
   result = eglInitialize( display, NULL, NULL);
   assert(EGL_FALSE != result);

   // get an appropriate EGL frame buffer configuration
   result = eglChooseConfig( display, attribute_list, &config, 1, &num_config);
   assert(EGL_FALSE != result);

   // get an appropriate EGL frame buffer configuration
   result = eglBindAPI(EGL_OPENGL_ES_API);
   assert(EGL_FALSE != result);
   
   // create an EGL rendering context
   context = eglCreateContext( display, config, EGL_NO_CONTEXT, context_attributes );
   assert( context != EGL_NO_CONTEXT );

   // create an EGL window surface
   success = graphics_get_display_size(0 /* LCD */, &screenWidth, &screenHeight);
   assert( success >= 0 );

   printf( "Screen size: %d x %d pixels\n", screenWidth, screenHeight );
   
   dst_rect.x = 0;
   dst_rect.y = 0;
   dst_rect.width = screenWidth;
   dst_rect.height = screenHeight;
      
   src_rect.x = 0;
   src_rect.y = 0;
   src_rect.width = screenWidth << 16;
   src_rect.height = screenHeight << 16;

   dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
   dispman_update = vc_dispmanx_update_start( 0 );
         
   dispman_element = vc_dispmanx_element_add ( dispman_update, dispman_display,
      0/*layer*/, &dst_rect, 0/*src*/,
      &src_rect, DISPMANX_PROTECTION_NONE, 0 /*alpha*/, 0/*clamp*/, 0/*transform*/);
      
   nativewindow.element = dispman_element;
   nativewindow.width = screenWidth;
   nativewindow.height = screenHeight;
   vc_dispmanx_update_submit_sync( dispman_update );
      
   surface = eglCreateWindowSurface(  display, config, &nativewindow, NULL );
   assert(surface != EGL_NO_SURFACE);

   // connect the context to the surface
   result = eglMakeCurrent( display, surface, surface, context);
   assert(EGL_FALSE != result);

}

// scale here is exponential (zoom)
void graphics_setMap( float scale, const TileCoordinate center )
{
  int zoomLevel;
  // tileToPixels will be 2^(11-24) = 2^-13
  double tileToPixels = pow( 2, scale - 24 );
  sTileCoordinate upperLeft, bottomRight, screenSizeTiles, topLeftTile, bottomRightTile;
  // int vertexCount;
  int /* vertexCountX, vertexCountY, */ x, y, i;
  int heightTiles, widthTiles;

  // makes ure all is good on entry
  assert( glGetError() == GL_NO_ERROR );

  // determine the preferred zoom scale of the tiles. Prefer enlarging tiles
  // to shrinking them, so round the scale down
  zoomLevel = floor( scale );

  tileCenter = *center;
  // figure out how many tiles to left and right and above and below center tile
  // screenSizeTiles.x = 600 / 2^-13= 600 * 2^13
  screenSizeTiles.x = screenWidth / tileToPixels;
  screenSizeTiles.y = screenHeight / tileToPixels;
  
  upperLeft.x = center->x - screenSizeTiles.x / 2;
  upperLeft.y = center->y + screenSizeTiles.y / 2;
  bottomRight.x = center->x + screenSizeTiles.x / 2;
  bottomRight.y = center->y - screenSizeTiles.y / 2;

  topLeftTile.x = upperLeft.x >> (32 - zoomLevel);
  topLeftTile.y = upperLeft.y >> (32 - zoomLevel); // round up Y
  uint32_t mask = ~((uint32_t) 0 );
  mask = mask >> zoomLevel;
  
  bottomRightTile.y = bottomRight.y >> (32 - zoomLevel);
  bottomRightTile.x = bottomRight.x >> (32 - zoomLevel);
#if 0
  printf( "Left tile: %d, right tile: %d\n", topLeftTile.x, bottomRightTile.x);
  printf( "Left edge: %.1f, righ edge: %.1f\n", (float) ((center->x - upperLeft.x) * tileToPixels), (float) ((bottomRight.x - center->x) * tileToPixels ));
#endif	  
  widthTiles = bottomRightTile.x - topLeftTile.x + 1;
  heightTiles = topLeftTile.y - bottomRightTile.y + 1;
  visibleTileCount = widthTiles * heightTiles;
  // trianglesCount = visibleTileCount * 2;

  // Center: 2362208165, 3025055848
  // Center tile: 
  // generate triangle list
  // vertexCountX = widthTiles + 1;
  // vertexCountY = heightTiles + 1;
  
  // tileStep = pow( 2, 32 - scale );

  glUseProgram( programObject );
  assert( glGetError() == GL_NO_ERROR );

  //
  for( y = 0, i = 0; y < heightTiles; y++ )
    for( x = 0; x < widthTiles; x++, i++ )
      {
	// char buf[256];
	// int success;
	// int width, height;
	int tileX, tileY;
	TileTexture tileTex;
	
	tileX = topLeftTile.x + x;
	tileY = topLeftTile.y - y;

	// start background loading of texture
	tileTex = tileTex_get( zoomLevel, tileX, tileY );
	tileTex_setVisible( tileTex, true );

	assert( i < MAX_TILES );
	
	tiles[i].tileTexture = tileTex;
	
	// top left corner. Largest Y, smallest X
	tiles[i].vertexData[0].position[0] =
	  (GLfloat) ( (((int64_t) tileX) << (32-zoomLevel)) - tileCenter.x);
	tiles[i].vertexData[0].position[1] =
	  (GLfloat) ( (((int64_t) (tileY + 1)) << (32-zoomLevel)) - tileCenter.y);
	// tiles[i].vertexData[0].position[2] = 0;

	// bottom left corner. Smallest Y smallest X
	tiles[i].vertexData[1].position[0] =
	  (GLfloat) ( (((int64_t) (topLeftTile.x + x)) << (32-zoomLevel)) - tileCenter.x);
	tiles[i].vertexData[1].position[1] =
	  (GLfloat) ( (((int64_t) (topLeftTile.y - y)) << (32-zoomLevel)) - tileCenter.y);
	// tiles[i].vertexData[1].position[2] = 0;

	// right bottom corner. Large X, small Y
	tiles[i].vertexData[2].position[0] =
	  (GLfloat) ((( (int64_t) (topLeftTile.x + x + 1)) << (32-zoomLevel)) -
		     tileCenter.x);
	tiles[i].vertexData[2].position[1] =
	  (GLfloat) ((( (int64_t) (topLeftTile.y - y)) << (32-zoomLevel)) -
		     tileCenter.y);
	// tiles[i].vertexData[2].position[2] = 0;

	// right upper corner. Large X, large Y
	tiles[i].vertexData[3].position[0] =
	  (GLfloat) ((( (int64_t) (topLeftTile.x + x + 1)) << (32-zoomLevel)) -
		     tileCenter.x);
	tiles[i].vertexData[3].position[1] =
	  (GLfloat) ((( (int64_t) (topLeftTile.y - y + 1)) << (32-zoomLevel)) -
		     tileCenter.y);
	// tiles[i].vertexData[3].position[2] = 0;

	float u0, u1, v0, v1;

	tileTex_getUV( tileTex, &u0, &v0, &u1, &v1 );
	
	tiles[i].vertexData[0].uv[0] = u0; // get from tileTex?
	tiles[i].vertexData[0].uv[1] = v0;

	tiles[i].vertexData[1].uv[0] = u0;
	tiles[i].vertexData[1].uv[1] = v1;

	tiles[i].vertexData[2].uv[0] = u1;
	tiles[i].vertexData[2].uv[1] = v1;

	tiles[i].vertexData[3].uv[0] = u1;
	tiles[i].vertexData[3].uv[1] = v0;
#if 0
	printf( "Using tile #%d: z %d %d/%d (%f,%f) - (%f,%f).\n", i,
		zoomLevel, tileX, tileY, tiles[i].vertexData[0].position[0],
		tiles[i].vertexData[0].position[1],
		tiles[i].vertexData[2].position[0],
		tiles[i].vertexData[2].position[1]);
#endif
      }
  assert( glGetError() == GL_NO_ERROR );
  // set up Index buffer stuff
  // this only needs to be done once
  glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, indexBufferID );
  glBufferData( GL_ELEMENT_ARRAY_BUFFER, sizeof( tileIndices ), tileIndices,
		GL_STATIC_DRAW ); // copy data to GPU

  assert( glGetError() == GL_NO_ERROR );

   //   glVertexAttribPointer( positionLoc, 3, 
   //   glEnableVertexAttribArray( positionLoc );
   
  // setup view matrix
}

/* coordinates are in tile coordinates. */
void graphics_redraw( float zoom, uint32_t top, uint32_t bottom, uint32_t left,
		      uint32_t right )
{
  // int tileZoom = floor( zoom );
  // at zoom level 11, one tile is 2^(32-11) tile units wide
  // this * 256 pixels = 2^(32-11+8) tile pixels
  // want 300 tile pixels to map to 1. 2^(32-11+8) ska bli 256 pixels = 256/300
  // 2^(32-11+8) * 300 * s / 256 = 1
  // s = 256 / (2^(32-11+8) * 300)
  //   = 1 / (2^(32-11) * 300)
  //   = 2^(11-32) / 300 
  // at zoom level 1, one tile is 2^31
  // float scale = screenWidth / pow( 2, 31 - zoom + 8); // in pixels / tile coordinate
  // float scale = pow( 2, (31 - zoom + 1) / screenWidth ; // in pixels / tile coordinate
  float scalex = 1.5 * pow( 2, zoom - 32 + 9 ) / screenWidth ; // in pixels / tile coordinate
  float scaley = 1.5 * pow( 2, zoom - 32 + 9 ) / screenHeight ; // in pixels / tile coordinate
  // zoom = 11. screenWidth = 600 -> scale = 600 / (2^28) = 
  int i;
  /* NOTE: must be -scale * tileCenter.x below, not scale * -tileCenter.x, or 
     it will try to make the uint32_t tileCenter.x signed, which will not give 
     the desired result */
  GLfloat scaleMatrix[] = { scalex, 0, 0, 0,
			    0, scaley, 0, 0,
			    0, 0, 1, 0,
			    // -scalex * tileCenter.x, -scaley * tileCenter.y, 0, 1 };
			    0, 0, 0, 1 };
  // left tile = 2 359 750 565 * scale = 5274
  // shift is scale * -tileCenter.x = 5279.9 -> -6, scale * -tileCenter.y = 3 025 055 848 * scale = 6762
  // right tile = 2 364 665 765 * scale = 5285
  // -> +6
  // top tile = 3 029 250 152 * scale = 6771 = +9
  // bottom tile = 3 020 861 544 * scale = 6752 = -10
  //
  // but we want these to map to +-1. 
  // 
  // 5274 - (2362208165 * 600 / 2^28) = 
  /*
  float scaleMatrix[] = { scale, 0, 0, scale * -tileCenter.x,
		  	  0, scale, 0, scale * -tileCenter.y,
			  0, 0, 1, 0,
			  0, 0, 0,      1 };
  */
  // float viewMatrix[16] = { };
  // float tx = 0; // -scalex * tileCenter.x;
  // float ty = 0; // -scaley * tileCenter.y;
  // uint32_t centerTileLeftEdge = tileCenter.x & (~((uint32_t) 0) << (32-(int)zoom));
  // uint32_t centerTileRightEdge = centerTileLeftEdge + (1 << (32-(int)zoom));
  //uint32_t centerTileBottomEdge =
  //  tileCenter.y & (~((uint32_t) 0) << (32-(int)zoom));
  // uint32_t centerTileTopEdge = centerTileBottomEdge + (1 << (32-(int)zoom));

  // float transformed[4];
#if 0
  printf( "scale=(%f,%f), tx=%f, ty=%f\n", scalex, scaley, tx, ty );
  printf( "tileCenter = %"PRIu32", %"PRIu32"\n", tileCenter.x, tileCenter.y );
  printf( "center x,y=%f, %f\n",
	  ( (int64_t) 2362208165 - tileCenter.x) * scalex + tx,
	  ( (int64_t) 3025055848 - tileCenter.y) * scaley + ty );
  
  printf( "Center tile left edge=%"PRIu32", @ %f\n", centerTileLeftEdge,
	  ( (int64_t) centerTileLeftEdge - tileCenter.x) * scalex + tx );
  printf( "Center tile right edge=%"PRIu32", @ %f\n", centerTileRightEdge,
	  ( (int64_t) centerTileRightEdge - tileCenter.x) * scalex + tx );
  
  printf( "Center tile top edge=%"PRIu32", @ %f\n", centerTileTopEdge,
	  ( (int64_t) centerTileTopEdge - tileCenter.y) * scaley + ty );
  printf( "Center tile bottom edge=%"PRIu32", @ %f\n", centerTileBottomEdge,
	  ( (int64_t) centerTileBottomEdge - tileCenter.y) * scaley + ty );
#endif
  glUseProgram( programObject );
  assert( glGetError() == GL_NO_ERROR );

  glViewport( 0, 0, screenWidth, screenHeight );
  assert( glGetError() == GL_NO_ERROR );

  // changes in OpenGL ES 2
  // glMatrixMode( GL_PROJECTION );
  // glLoadIdentity();
  // glOrthof( left, right, top, bottom, -1, 1 );

  // glMatrixMode( GL_MODELVIEW );
  // glLoadIdentity();

  // clear the screen
  glClear( GL_COLOR_BUFFER_BIT );
  assert( glGetError() == GL_NO_ERROR );
  
  glUniformMatrix4fv( viewMatrixLoc, 1, GL_FALSE, scaleMatrix);
  assert( glGetError() == GL_NO_ERROR );

  // wait for textures to be loaded
  tileTex_waitVisibleLoaded();
  
  // loop over tiles
  // it seems inefficent to have a triangle strip for each quad, but I spent
  // a day trying to figure how I could do a Triangle Strip with different
  // textures in each triangle, but without success.
  for( i = 0; i < visibleTileCount; i++ )
  {
    TileData tile = &(tiles[i]);
    float transformed[4][2];
    TileTexture tileTex;
    GLuint textureID;
    
    tileTex = tile->tileTexture;
    /*
    if( !tileTex_isLoaded( tileTex ) )
      continue;
    */
    textureID = tileTex_makeTextureID( tileTex );

    if( textureID == -1 )
    {
      // printf( "Skipping tile %d because it has no textureID\n", i );
      continue;
    }

      // set up Vertex buffer stuff
    if( tiles[i].vertexBufferID == -1 )
      glGenBuffers( 1, &(tiles[i].vertexBufferID) );
    
    glBindBuffer( GL_ARRAY_BUFFER, tiles[ i ].vertexBufferID );
    assert( glGetError() == GL_NO_ERROR );
    glBufferData( GL_ARRAY_BUFFER, sizeof( tiles[i].vertexData ),
		  tiles[i].vertexData, GL_STATIC_DRAW ); // copy data to GPU
    assert( glGetError() == GL_NO_ERROR );
    // generate texture id

    // debug
    transformVertex( tile, scaleMatrix, (float *) &transformed );
#if 0
    printf( "tile %d transformed to (%f,%f), (%f,%f), (%f,%f), (%f,%f)\n",
	    i,
	    transformed[0][0], transformed[0][1],
	    transformed[1][0], transformed[1][1],
	    transformed[2][0], transformed[2][1],
	    transformed[3][0], transformed[3][1] );
#endif
    glBindTexture( GL_TEXTURE_2D, textureID );
    assert( glGetError() == GL_NO_ERROR );

    glBindBuffer( GL_ARRAY_BUFFER, tile->vertexBufferID );
    assert( glGetError() == GL_NO_ERROR );
    
    glVertexAttribPointer( positionLoc, 2, GL_FLOAT /* was uint */, GL_FALSE,
			   sizeof(sVertexData),
			   (void *) offsetof( sVertexData, position));
    assert( glGetError() == GL_NO_ERROR );
    glVertexAttribPointer( texCoordLoc, 2, GL_FLOAT, GL_FALSE,
			   sizeof(sVertexData), (void *) offsetof( sVertexData, uv ) );
    assert( glGetError() == GL_NO_ERROR );

    glEnableVertexAttribArray( positionLoc );
    glEnableVertexAttribArray( texCoordLoc );
    
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, indexBufferID );
    assert( glGetError() == GL_NO_ERROR );

    glDrawElements( GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, NULL );

    assert( glGetError() == GL_NO_ERROR );
  }

  eglSwapBuffers( display, surface );
  
}

static GLuint LoadProgram ( const char *vertShaderSrc, const char *fragShaderSrc )
{
  GLuint vertexShader;
  GLuint fragmentShader;
  GLuint programObject;
  GLint linked;

  // Load the vertex/fragment shaders
  vertexShader = LoadShader ( GL_VERTEX_SHADER, vertShaderSrc );
  if ( vertexShader == 0 )
    return 0;

  fragmentShader = LoadShader ( GL_FRAGMENT_SHADER, fragShaderSrc );
  if ( fragmentShader == 0 )
    {
      glDeleteShader( vertexShader );
      return 0;
    }

  // Create the program object
  programObject = glCreateProgram ( );

  if ( programObject == 0 )
    return 0;

  glAttachShader ( programObject, vertexShader );
  glAttachShader ( programObject, fragmentShader );

  // Link the program
  glLinkProgram ( programObject );

  // Check the link status
  glGetProgramiv ( programObject, GL_LINK_STATUS, &linked );

  if ( !linked )
    {
      GLint infoLen = 0;
      glGetProgramiv ( programObject, GL_INFO_LOG_LENGTH, &infoLen );

      if ( infoLen > 1 )
	{
	  char* infoLog = malloc (sizeof(char) * infoLen );

	  glGetProgramInfoLog ( programObject, infoLen, NULL, infoLog );
	  fprintf (stderr, "Error linking program:\n%s\n", infoLog );

	  free ( infoLog );
	}

      glDeleteProgram ( programObject );
      return 0;
    }

  // Free up no longer needed shader resources
  glDeleteShader ( vertexShader );
  glDeleteShader ( fragmentShader );

  return programObject;
}

///
// Create a shader object, load the shader source, and
// compile the shader.
//
static GLuint LoadShader(GLenum type, const char *shaderSrc)
{
  GLuint shader;
  GLint compiled;
  // Create the shader object
  shader = glCreateShader(type);
  if(shader == 0)
    return 0;
  // Load the shader source
  glShaderSource(shader, 1, &shaderSrc, NULL);
  // Compile the shader
  glCompileShader(shader);
  // Check the compile status
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if(!compiled)
    {
      GLint infoLen = 0;
      glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
      if(infoLen > 1)
	{
	  char* infoLog = malloc(sizeof(char) * infoLen);
	  glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
	  fprintf(stderr, "Error compiling shader:\n%s\n", infoLog);
	  free(infoLog);
	}
      glDeleteShader(shader);
      return 0;
    }
  return shader;
}

// for debugging
static void transformVertex( const TileData tile, const float *scaleMatrix,
			     float *transformed )
{
  int i;
  for( i = 0; i < 4; i++ )
  {
    VertexData vd = &(tile->vertexData[i]);
    
    transformed[ i * 2 ] = vd->position[0] * scaleMatrix[0] +
      vd->position[1] * scaleMatrix[ 4 ] + scaleMatrix[ 12 ];
    transformed[ i * 2 + 1 ] =
      vd->position[0] * scaleMatrix[ 1 ] +
      vd->position[1] * scaleMatrix[ 5 ] +
      scaleMatrix[ 13 ];
  }
}
