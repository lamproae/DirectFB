// #define DIRECT_ENABLE_DEBUG

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string.h>

#include <directfb_util.h>
#include <gfx/convert.h>
#include <core/system.h>


#include "lxc_gfxdriver.h"
#include "lxc_layer.h"

D_DEBUG_DOMAIN( lxc_Layer, "LXC/Layer", "LXC Layer" );

/*****************************************************************************/

static int
lxcLayerDataSize(
     void )
{
     return sizeof( LXCLayerData );
}

static int
lxcRegionDataSize(
     void )
{
     return sizeof( LXCRegionData );
}

static DFBResult
lxcInitLayer(
     CoreLayer * layer,
     void *driver_data,
     void *layer_data,
     DFBDisplayLayerDescription * description,
     DFBDisplayLayerConfig * config,
     DFBColorAdjustment * adjustment )
{
     LXCLayerData *lxc_layer = layer_data;
     struct sockaddr_un remote;

     D_DEBUG_AT( lxc_Layer, "%s()\n", __FUNCTION__ );

     /*
      * open the socket to the host 
      */
     lxc_layer->socket_to_host = socket( AF_UNIX, SOCK_STREAM, 0 );
     if ( lxc_layer->socket_to_host == -1 ) {
          D_PERROR( "LXC layer: socket() failed\n" );
          return DFB_INIT;
     }

     remote.sun_family = AF_UNIX;
     strcpy( remote.sun_path, SOCK_PATH );

     /*
      * connect to the socket 
      */
     if ( connect( lxc_layer->socket_to_host, ( struct sockaddr * ) &remote,
                   strlen( remote.sun_path ) + sizeof( remote.sun_family ) ) ==
          -1 ) {
          D_PERROR( "LXC layer: connect(%s) failed\n", SOCK_PATH );
          return DFB_INIT;
     }

     /*
      * set capabilities and type 
      */
     description->caps = DLCAPS_SURFACE;
     description->type = DLTF_GRAPHICS | DLTF_VIDEO | DLTF_BACKGROUND;

     /*
      * set name 
      */
     snprintf( description->name, DFB_DISPLAY_LAYER_DESC_NAME_LENGTH,
               "Primary layer for LXC" );

     /*
      * fill out the default configuration 
      */
     config->flags =
         DLCONF_WIDTH | DLCONF_HEIGHT | DLCONF_PIXELFORMAT | DLCONF_BUFFERMODE |
         DLCONF_OPTIONS;

     /*
      * for now we are hardccoding our layer parameters
      */
     config->width = XRESOLUTION;
     config->height = YRESOLUTION;
     config->pixelformat = DSPF_RGB16;
     config->buffermode = DLBM_FRONTONLY;
     config->options = DLOP_NONE;

     return DFB_OK;
}

static DFBResult
lxcShutdownLayer(
     CoreLayer * layer,
     void *driver_data,
     void *layer_data )
{
     LXCLayerData *lxc_layer = layer_data;

     if ( lxc_layer->socket_to_host != -1 ) {
          close( lxc_layer->socket_to_host );
          lxc_layer->socket_to_host = -1;
     }

     return DFB_OK;
}

static void
update_rgb(
     LXCDriverData * ddrv,
     LXCDeviceData * ddev,
     CoreSurface * surface,
     CoreSurfaceBufferLock * lock,
     const DFBRegion * update )
{
     DFBRectangle rect;
     CoreSurfaceBuffer *buffer;

     D_ASSERT( ddrv != NULL );
     D_ASSERT( ddev != NULL );
     D_ASSERT( surface != NULL );
     D_ASSERT( lock != NULL );
     DFB_REGION_ASSERT_IF( update );

     buffer = lock->buffer;
     D_ASSERT( buffer != NULL );

     if ( update )
          rect = DFB_RECTANGLE_INIT_FROM_REGION( update );
     else {
          rect.x = 0;
          rect.y = 0;
          rect.w = surface->config.size.w;
          rect.h = surface->config.size.h;
     }

     /*
      * We need to convert the pixel format to RGB16 so that the
      * host doesn't have to guess.
      */
     dfb_convert_to_rgb16( buffer->format,
                           lock->addr + rect.y * lock->pitch +
                           DFB_BYTES_PER_LINE( buffer->format, rect.x ),
                           lock->pitch, NULL, 0, NULL, 0,
                           surface->config.size.h,
                           dfb_system_video_memory_virtual( 0 ) +
                           rect.y * DFB_BYTES_PER_LINE( DSPF_RGB16,
                                                        XRESOLUTION ) +
                           rect.x * DFB_BYTES_PER_PIXEL( DSPF_RGB16 ),
                           DFB_BYTES_PER_LINE( DSPF_RGB16, XRESOLUTION ),
                           rect.w, rect.h );
}

static DFBResult
lxcTestRegion(
     CoreLayer * layer,
     void *driver_data,
     void *layer_data,
     CoreLayerRegionConfig * config,
     CoreLayerRegionConfigFlags * failed )
{
     CoreLayerRegionConfigFlags fail = 0;

     DFB_CORE_LAYER_REGION_CONFIG_DEBUG_AT( lxc_Layer, config );

     if ( config->options & ~LXC_LAYER_SUPPORTED_OPTIONS ) {
          fail |= CLRCF_OPTIONS;
     }

     switch ( config->format ) {
          /*
           * We accept all kinds of pixel formats (the ones supported by
           * dfb_convert_to_rgb16()) but we will have to convert
           * them to RGB16 later.
           */
     case DSPF_RGB16:
     case DSPF_NV16:
     case DSPF_UYVY:
     case DSPF_RGB444:
     case DSPF_ARGB4444:
     case DSPF_RGBA4444:
     case DSPF_RGB555:
     case DSPF_ARGB1555:
     case DSPF_BGR555:
     case DSPF_RGB32:
     case DSPF_ARGB:
     case DSPF_ABGR:
     case DSPF_RGBAF88871:
     case DSPF_AYUV:
     case DSPF_AVYU:
     case DSPF_VYU:
     case DSPF_YUY2:
     case DSPF_RGBA5551:
     case DSPF_YUV444P:
     case DSPF_ARGB8565:
     case DSPF_YV16:
          break;

     default:
          fail |= CLRCF_FORMAT;
     }

     if ( config->width != XRESOLUTION )
          fail |= CLRCF_WIDTH;

     if ( config->height != YRESOLUTION )
          fail |= CLRCF_HEIGHT;

     if ( config->dest.x < 0 || config->dest.y < 0 )
          fail |= CLRCF_DEST;

     if ( config->dest.x + config->dest.w > XRESOLUTION )
          fail |= CLRCF_DEST;

     if ( config->dest.y + config->dest.h > YRESOLUTION )
          fail |= CLRCF_DEST;

     if ( failed )
          *failed = fail;

     if ( fail ) {
          return DFB_UNSUPPORTED;
     }

     return DFB_OK;
}

static DFBResult
lxcSetRegion(
     CoreLayer * layer,
     void *driver_data,
     void *layer_data,
     void *region_data,
     CoreLayerRegionConfig * config,
     CoreLayerRegionConfigFlags updated,
     CoreSurface * surface,
     CorePalette * palette,
     CoreSurfaceBufferLock * left_lock,
     CoreSurfaceBufferLock * right_lock )
{
     /*
      * nothing to do in SetRegion
      */
     return DFB_OK;
}

static DFBResult
lxcFlipRegion(
     CoreLayer * layer,
     void *driver_data,
     void *layer_data,
     void *region_data,
     CoreSurface * surface,
     DFBSurfaceFlipFlags flags,
     const DFBRegion * left_update,
     CoreSurfaceBufferLock * left_lock,
     const DFBRegion * right_update,
     CoreSurfaceBufferLock * right_lock )
{
     CoreSurfaceBuffer *buffer;
     LXCDriverData *ddrv = driver_data;
     LXCDeviceData *ddev = ddrv->ddev;

     D_DEBUG_AT( lxc_Layer, "%s()\n", __FUNCTION__ );

     D_ASSERT( surface != NULL );
     D_ASSERT( left_lock != NULL );
     D_ASSERT( ddrv != NULL );
     D_ASSERT( ddev != NULL );

     buffer = left_lock->buffer;
     D_ASSERT( buffer != NULL );

     if ( buffer->format != DSPF_RGB16 ) {
          update_rgb( ddrv, ddev, surface, left_lock, NULL );
     }

     dfb_surface_flip( surface, false );

     return DFB_OK;
}

static DFBResult
lxcUpdateRegion(
     CoreLayer * layer,
     void *driver_data,
     void *layer_data,
     void *region_data,
     CoreSurface * surface,
     const DFBRegion * left_update,
     CoreSurfaceBufferLock * left_lock,
     const DFBRegion * right_update,
     CoreSurfaceBufferLock * right_lock )
{
     CoreSurfaceBuffer *buffer;
     LXCDriverData *ddrv = driver_data;
     LXCDeviceData *ddev = ddrv->ddev;
     LXCLayerData *lxc_layer = layer_data;

     D_DEBUG_AT( lxc_Layer, "%s()\n", __FUNCTION__ );

     D_ASSERT( surface != NULL );
     D_ASSERT( left_lock != NULL );
     D_ASSERT( ddrv != NULL );
     D_ASSERT( ddev != NULL );

     buffer = left_lock->buffer;
     D_ASSERT( buffer != NULL );

     if ( buffer->format != DSPF_RGB16 ) {
          update_rgb( ddrv, ddev, surface, left_lock, left_update );
     }

     /*
      * we tell the host that the layer is updated
      * The host uses this layer as a surface before sending it to the
      * final (real) layer (fb, X11, SDL,...).
      */
     if ( lxc_layer->socket_to_host != -1 ) {
          char flags = 1;
          int ret =
              send( lxc_layer->socket_to_host, &flags, sizeof( flags ), 0 );
          if ( ret == -1 ) {
               D_ASSERT( ret != -1 );
          }
     }

     return DFB_OK;
}

const DisplayLayerFuncs lxcLayerFuncs = {
     .LayerDataSize = lxcLayerDataSize,
     .RegionDataSize = lxcRegionDataSize,

     .InitLayer = lxcInitLayer,
     .ShutdownLayer = lxcShutdownLayer,

     .TestRegion = lxcTestRegion,
     .SetRegion = lxcSetRegion,
     .FlipRegion = lxcFlipRegion,
     .UpdateRegion = lxcUpdateRegion,
};
