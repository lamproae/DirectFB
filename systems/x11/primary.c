/*
   (c) Copyright 2012-2013  DirectFB integrated media GmbH
   (c) Copyright 2001-2013  The world wide DirectFB Open Source Community (directfb.org)
   (c) Copyright 2000-2004  Convergence (integrated media) GmbH

   All rights reserved.

   Written by Denis Oliver Kropp <dok@directfb.org>,
              Andreas Shimokawa <andi@directfb.org>,
              Marek Pikarski <mass@directfb.org>,
              Sven Neumann <neo@directfb.org>,
              Ville Syrjälä <syrjala@sci.fi> and
              Claudio Ciccani <klan@users.sf.net>.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/



//#define DIRECT_ENABLE_DEBUG

#include <config.h>

#include <fusion/types.h>

#include <stdio.h>

#include <directfb.h>
#include <directfb_util.h>

#include <fusion/fusion.h>
#include <fusion/shmalloc.h>

#include <core/Debug.h>
#include <core/Task.h>

#include <core/core.h>
#include <core/coredefs.h>
#include <core/coretypes.h>
#include <core/layers.h>
#include <core/layers_internal.h>
#include <core/palette.h>
#include <core/surface.h>
#include <core/surface_buffer.h>
#include <core/system.h>

#include <gfx/convert.h>

#include <misc/conf.h>

#include <direct/memcpy.h>
#include <direct/messages.h>

#include <string.h>
#include <stdlib.h>

#ifdef USE_GLX
#include <GL/glx.h>
#include "glx_surface_pool.h"
#endif

#include "x11_surface_pool.h"

#include "xwindow.h"
#include "x11.h"
#include "primary.h"

D_DEBUG_DOMAIN( X11_Layer,  "X11/Layer",  "X11 Layer" );
D_DEBUG_DOMAIN( X11_Update, "X11/Update", "X11 Update" );

/**********************************************************************************************************************/

static DFBResult
dfb_x11_create_window( DFBX11 *x11, X11LayerData *lds, const CoreLayerRegionConfig *config )
{
     int           ret;
     DFBX11Shared *shared = x11->shared;

     D_ASSERT( config != NULL );

     shared->setmode.config   = *config;
     shared->setmode.xw       = &(shared->output[lds->layer_id/3].xw);
     shared->setmode.layer_id = lds->layer_id;
     shared->setmode.output   = lds->layer_id/shared->layers;

     if (fusion_call_execute( &shared->call, FCEF_NONE, X11_CREATE_WINDOW, &shared->setmode, &ret ))
          return DFB_FUSION;

     return ret;
}

static DFBResult
dfb_x11_destroy_window( DFBX11 *x11, X11LayerData *lds )
{
//     if (!lds->xw)
//          return DFB_OK;

//     int           ret;
//     DFBX11Shared *shared = x11->shared;
//
//     shared->destroy.xw = &(lds->xw);
//
//     if (fusion_call_execute( &shared->call, FCEF_NONE, X11_DESTROY_WINDOW, &shared->destroy, &ret ))
//          return DFB_FUSION;

     return DFB_OK;//ret;
}

static DFBResult
update_stereo( DFBX11 *x11, const DFBRectangle *left_clip, const DFBRectangle *right_clip,
               CoreSurfaceBufferLock *left_lock, CoreSurfaceBufferLock *right_lock, XWindow *xw );

static DFBResult
update_screen( DFBX11 *x11, const DFBRectangle *clip, CoreSurfaceBufferLock *lock, XWindow *xw );

DFBResult
dfb_x11_update_screen( DFBX11 *x11, X11LayerData *lds, const DFBRegion *left_region, const DFBRegion *right_region,
                       CoreSurfaceBufferLock *left_lock, CoreSurfaceBufferLock *right_lock )
{
     int           ret;
     DFBX11Shared *shared = x11->shared;

     DFB_REGION_ASSERT( left_region );
//     D_ASSERT( left_lock != NULL );

     /* FIXME: Just a hot fix! */
     if (shared->update.left_lock.buffer) {
          D_ONCE( "using x11 update hotfix" );
          return DFB_OK;
     }

     shared->update.xw           = shared->output[lds->layer_id/shared->layers].xw;
     shared->update.left_region  = *left_region;
     shared->update.left_lock    = *left_lock;

     shared->update.stereo       = (lds->config.options & DLOP_STEREO);

     if (shared->update.stereo) {
          DFB_REGION_ASSERT( right_region );
//          D_ASSERT( right_lock != NULL );

          shared->update.right_region = *right_region;
//          shared->update.right_lock   = *right_lock;
     }


     UpdateScreenData *data = &shared->update;

     if (data->stereo) {
          DFBRectangle left_rect;
          DFBRectangle right_rect;

          left_rect  = DFB_RECTANGLE_INIT_FROM_REGION( &data->left_region );
          right_rect = DFB_RECTANGLE_INIT_FROM_REGION( &data->right_region );

//          if (data->left_lock.buffer && data->right_lock.buffer)
               update_stereo( x11, &left_rect, &right_rect, &data->left_lock, &data->right_lock, data->xw );
     }
     else {
          DFBRectangle rect;

          rect = DFB_RECTANGLE_INIT_FROM_REGION( &data->left_region );

//          if (data->left_lock.buffer)
               update_screen( x11, &rect, &data->left_lock, data->xw );
     }

     data->left_lock.buffer  = NULL;
     data->right_lock.buffer = NULL;

     return DFB_OK;


     if (fusion_call_execute( &shared->call, FCEF_NODIRECT, X11_UPDATE_SCREEN, &shared->update, &ret ))
          return DFB_FUSION;

     return ret;
}

static DFBResult
dfb_x11_set_palette( DFBX11 *x11, X11LayerData *lds, CorePalette *palette )
{
     int           ret;
     DFBX11Shared *shared = x11->shared;

     D_ASSERT( palette != NULL );

     if (fusion_call_execute( &shared->call, FCEF_NONE, X11_SET_PALETTE, palette, &ret ))
          return DFB_FUSION;

     return ret;
}

/**********************************************************************************************************************/

static DFBResult
primaryInitScreen( CoreScreen           *screen,
                   CoreGraphicsDevice   *device,
                   void                 *driver_data,
                   void                 *screen_data,
                   DFBScreenDescription *description )
{
     DFBX11       *x11    = driver_data;
     DFBX11Shared *shared = x11->shared;

     D_DEBUG_AT( X11_Layer, "%s()\n", __FUNCTION__ );

     /* Set the screen capabilities. */
     description->caps     = DSCCAPS_MIXERS | DSCCAPS_ENCODERS | DSCCAPS_OUTPUTS;
     description->mixers   = shared->outputs;
     description->encoders = shared->outputs;
     description->outputs  = shared->outputs;

     /* Set the screen name. */
     snprintf( description->name,
               DFB_SCREEN_DESC_NAME_LENGTH, "X11 Primary Screen" );

     return DFB_OK;
}

static DFBResult
primaryShutdownScreen( CoreScreen *screen,
                       void       *driver_data,
                       void       *screen_data )
{
     int           i;
     DFBX11       *x11    = driver_data;
     DFBX11Shared *shared = x11->shared;

     D_DEBUG_AT( X11_Layer, "%s()\n", __FUNCTION__ );

     for (i=0; i<shared->outputs; i++) {
          if (shared->output[i].surface)
               dfb_surface_unref( shared->output[i].surface );

          CoreGraphicsStateClient_Deinit( &shared->output[i].client );

          dfb_state_destroy( &shared->output[i].gfx_state );
     }

     return DFB_OK;
}

static DFBResult
primaryGetScreenSize( CoreScreen *screen,
                      void       *driver_data,
                      void       *screen_data,
                      int        *ret_width,
                      int        *ret_height )
{
     DFBX11       *x11    = driver_data;
     DFBX11Shared *shared = x11->shared;

     D_DEBUG_AT( X11_Layer, "%s()\n", __FUNCTION__ );

     *ret_width  = shared->screen_size.w;
     *ret_height = shared->screen_size.h;

     return DFB_OK;
}

static DFBResult
primaryInitMixer( CoreScreen                *screen,
                  void                      *driver_data,
                  void                      *screen_data,
                  int                        mixer,
                  DFBScreenMixerDescription *description,
                  DFBScreenMixerConfig      *config )
{
     DFBX11       *x11    = driver_data;
     DFBX11Shared *shared = x11->shared;

     (void) shared;

     D_DEBUG_AT( X11_Layer, "%s()\n", __FUNCTION__ );

     direct_snputs( description->name, "X11 Mixer", DFB_SCREEN_ENCODER_DESC_NAME_LENGTH );

     description->caps   = DSMCAPS_FULL;
     description->layers = ((1 << shared->layers)-1) << (mixer * shared->layers);

     config->flags       = DSMCONF_LAYERS;
     config->layers      = description->layers;

     return DFB_OK;
}

static DFBResult
primaryTestMixerConfig( CoreScreen                 *screen,
                        void                       *driver_data,
                        void                       *screen_data,
                        int                         mixer,
                        const DFBScreenMixerConfig *config,
                        DFBScreenMixerConfigFlags  *failed )
{
     DFBX11       *x11    = driver_data;
     DFBX11Shared *shared = x11->shared;

     (void) shared;

     D_DEBUG_AT( X11_Layer, "%s()\n", __FUNCTION__ );

     return DFB_OK;
}

static DFBResult
primarySetMixerConfig( CoreScreen                 *screen,
                       void                       *driver_data,
                       void                       *screen_data,
                       int                         mixer,
                       const DFBScreenMixerConfig *config )
{
     D_DEBUG_AT( X11_Layer, "%s()\n", __FUNCTION__ );

     return DFB_OK;
}

static DFBResult
primaryInitEncoder( CoreScreen                  *screen,
                    void                        *driver_data,
                    void                        *screen_data,
                    int                          encoder,
                    DFBScreenEncoderDescription *description,
                    DFBScreenEncoderConfig      *config )
{
     DFBX11       *x11    = driver_data;
     DFBX11Shared *shared = x11->shared;

     (void) shared;

     D_DEBUG_AT( X11_Layer, "%s()\n", __FUNCTION__ );

     direct_snputs( description->name, "X11 Encoder", DFB_SCREEN_ENCODER_DESC_NAME_LENGTH );

     description->caps            = DSECAPS_TV_STANDARDS | DSECAPS_SCANMODE   | DSECAPS_FREQUENCY |
                                    DSECAPS_CONNECTORS   | DSECAPS_RESOLUTION | DSECAPS_FRAMING;
     description->type            = DSET_DIGITAL;
     description->tv_standards    = DSETV_DIGITAL;
     description->all_connectors  = DSOC_COMPONENT | DSOC_HDMI;
     description->all_resolutions = DSOR_640_480  | DSOR_720_480   | DSOR_720_576   | DSOR_800_600 |
                                    DSOR_1024_768 | DSOR_1152_864  | DSOR_1280_720  | DSOR_1280_768 |
                                    DSOR_1280_960 | DSOR_1280_1024 | DSOR_1400_1050 | DSOR_1600_1200 |
                                    DSOR_1920_1080 | DSOR_960_540 | DSOR_1440_540;

     config->flags          = DSECONF_TV_STANDARD | DSECONF_SCANMODE   | DSECONF_FREQUENCY |
                              DSECONF_CONNECTORS  | DSECONF_RESOLUTION | DSECONF_FRAMING | DSECONF_MIXER;
     config->tv_standard    = DSETV_DIGITAL;
     config->out_connectors = DSOC_COMPONENT | DSOC_HDMI;
     config->scanmode       = DSESM_PROGRESSIVE;
     config->frequency      = DSEF_60HZ;
     config->framing        = DSEPF_MONO;
     config->resolution     = DSOR_1280_720;
     config->mixer          = encoder;

     shared->output[encoder].size.w = 1280;
     shared->output[encoder].size.h = 720;

     return DFB_OK;
}

static DFBResult
primaryTestEncoderConfig( CoreScreen                   *screen,
                          void                         *driver_data,
                          void                         *screen_data,
                          int                           encoder,
                          const DFBScreenEncoderConfig *config,
                          DFBScreenEncoderConfigFlags  *failed )
{
     DFBX11       *x11    = driver_data;
     DFBX11Shared *shared = x11->shared;

     (void) shared;

     D_DEBUG_AT( X11_Layer, "%s()\n", __FUNCTION__ );

     return DFB_OK;
}

static DFBResult
primarySetEncoderConfig( CoreScreen                   *screen,
                         void                         *driver_data,
                         void                         *screen_data,
                         int                           encoder,
                         const DFBScreenEncoderConfig *config )
{
     DFBX11       *x11    = driver_data;
     DFBX11Shared *shared = x11->shared;

     int hor[] = { 640,720,720,800,1024,1152,1280,1280,1280,1280,1400,1600,1920 };
     int ver[] = { 480,480,576,600, 768, 864, 720, 768, 960,1024,1050,1200,1080 };

     int res;

     D_DEBUG_AT( X11_Layer, "%s()\n", __FUNCTION__ );

     (void)encoder; /* all outputs are active */

     res = D_BITn32(config->resolution);
     if ( (res == -1) || (res >= D_ARRAY_SIZE(hor)) )
          return DFB_INVARG;

     shared->output[encoder].size.w = hor[res];
     shared->output[encoder].size.h = ver[res];

     shared->screen_size.w = hor[res];
     shared->screen_size.h = ver[res];

     return DFB_OK;
}

static DFBResult
primaryInitOutput( CoreScreen                   *screen,
                   void                         *driver_data,
                   void                         *screen_data,
                   int                           output,
                   DFBScreenOutputDescription   *description,
                   DFBScreenOutputConfig        *config )
{
     DFBX11       *x11    = driver_data;
     DFBX11Shared *shared = x11->shared;

     (void) shared;

     D_DEBUG_AT( X11_Layer, "%s()\n", __FUNCTION__ );

     direct_snputs( description->name, "X11 Output", DFB_SCREEN_OUTPUT_DESC_NAME_LENGTH );

     description->caps = DSOCAPS_RESOLUTION;

     config->flags      |= DSOCONF_RESOLUTION | DSOCONF_ENCODER;
     config->resolution  = DSOR_UNKNOWN;
     config->encoder     = output;




     DFBResult         ret;
     CoreSurfaceConfig sconfig;

     sconfig.flags                  = CSCONF_SIZE | CSCONF_FORMAT | CSCONF_CAPS;
     sconfig.size.w                 = shared->screen_size.w;
     sconfig.size.h                 = shared->screen_size.h;
     sconfig.format                 = DSPF_ARGB;
     sconfig.caps                   = DSCAPS_NONE;//SYSTEMONLY;// | DSCAPS_DOUBLE;// | DSCAPS_SHARED;

     ret = dfb_surface_create( x11->core, &sconfig, CSTF_NONE, 0,
                               NULL, &shared->output[output].surface );
     if (ret) {
          D_DERROR( ret, "DirectFB/X11: Could not createscreen surface!\n" );
          return ret;
     }

//     dfb_surface_lock_buffer( shared->output[output].surface, 0, CSAID_CPU, CSAF_WRITE, &shared->output[output].lock );

     dfb_state_init( &shared->output[output].gfx_state, x11->core );
     CoreGraphicsStateClient_Init( &shared->output[output].client, &shared->output[output].gfx_state );

     return DFB_OK;
}

static DFBResult
primaryTestOutputConfig( CoreScreen                  *screen,
                         void                        *driver_data,
                         void                        *screen_data,
                         int                          output,
                         const DFBScreenOutputConfig *config,
                         DFBScreenOutputConfigFlags  *failed )
{
     DFBX11       *x11    = driver_data;
     DFBX11Shared *shared = x11->shared;

     (void) shared;

     D_DEBUG_AT( X11_Layer, "%s()\n", __FUNCTION__ );

     return DFB_OK;
}

static DFBResult
primarySetOutputConfig( CoreScreen                  *screen,
                        void                        *driver_data,
                        void                        *screen_data,
                        int                          output,
                        const DFBScreenOutputConfig *config )
{
     DFBX11       *x11    = driver_data;
     DFBX11Shared *shared = x11->shared;

     int hor[] = { 640,720,720,800,1024,1152,1280,1280,1280,1280,1400,1600,1920 };
     int ver[] = { 480,480,576,600, 768, 864, 720, 768, 960,1024,1050,1200,1080 };

     int res;

     D_DEBUG_AT( X11_Layer, "%s()\n", __FUNCTION__ );

     (void)output; /* all outputs are active */

     /* we support screen resizing only */
     if (config->flags != DSOCONF_RESOLUTION)
          return DFB_INVARG;

     res = D_BITn32(config->resolution);
     if ( (res == -1) || (res >= D_ARRAY_SIZE(hor)) )
          return DFB_INVARG;

     shared->output[output].size.w = hor[res];
     shared->output[output].size.h = ver[res];

     shared->screen_size.w = hor[res];
     shared->screen_size.h = ver[res];

     return DFB_OK;
}

static ScreenFuncs primaryScreenFuncs = {
     .InitScreen        = primaryInitScreen,
     .ShutdownScreen    = primaryShutdownScreen,
     .GetScreenSize     = primaryGetScreenSize,
     .InitMixer         = primaryInitMixer,
     .TestMixerConfig   = primaryTestMixerConfig,
     .SetMixerConfig    = primarySetMixerConfig,
     .InitEncoder       = primaryInitEncoder,
     .TestEncoderConfig = primaryTestEncoderConfig,
     .SetEncoderConfig  = primarySetEncoderConfig,
     .InitOutput        = primaryInitOutput,
     .TestOutputConfig  = primaryTestOutputConfig,
     .SetOutputConfig   = primarySetOutputConfig
};

ScreenFuncs *x11PrimaryScreenFuncs = &primaryScreenFuncs;

/******************************************************************************/

static int
primaryLayerDataSize( void )
{
     return sizeof(X11LayerData);
}

static int
primaryRegionDataSize( void )
{
     return 0;
}

static DFBResult
primaryInitLayer( CoreLayer                  *layer,
                  void                       *driver_data,
                  void                       *layer_data,
                  DFBDisplayLayerDescription *description,
                  DFBDisplayLayerConfig      *config,
                  DFBColorAdjustment         *adjustment )
{
     DFBX11       *x11    = driver_data;
     DFBX11Shared *shared = x11->shared;
     X11LayerData *lds    = layer_data;
     int           output;
     char         *name;

     D_DEBUG_AT( X11_Layer, "%s()\n", __FUNCTION__ );

     {
          static int layer_counter = 0;

          int index = layer_counter%shared->layers;

          if (index > 2)
               index = 2;

          char *names[] = { "Primary", "Secondary", "Tertiary" };
          name = names[index];

          output = layer_counter/shared->layers;

          shared->output[output].layers[layer_counter%shared->layers] = lds;

          lds->layer_id = layer_counter;
          layer_counter++;
     }

     /* set capabilities and type */
     description->caps = DLCAPS_SURFACE | DLCAPS_LR_MONO | DLCAPS_STEREO | DLCAPS_SCREEN_POSITION;// | DLCAPS_SCREEN_SIZE;
     description->type = DLTF_GRAPHICS;
     description->surface_accessor = CSAID_CPU;

     /* set name */
     snprintf( description->name,
               DFB_DISPLAY_LAYER_DESC_NAME_LENGTH, "X11 %s Layer", name );

     if (lds->layer_id % shared->layers == 0) {
          bool bSucces = dfb_x11_open_window( x11, &shared->output[output].xw, dfb_config->x11_position.x, dfb_config->x11_position.y,
                                              shared->output[output].size.w, shared->output[output].size.h,
                                              DSPF_RGB32 );

          shared->output[output].xw->layer_id = lds->layer_id;
     }

     /* fill out the default configuration */
     config->flags       = DLCONF_WIDTH       | DLCONF_HEIGHT |
                           DLCONF_PIXELFORMAT | DLCONF_BUFFERMODE |
                           DLCONF_OPTIONS;
     config->buffermode  = DLBM_FRONTONLY;
     config->options     = DLOP_NONE;

     if (dfb_config->mode.width)
          config->width  = dfb_config->mode.width;
     else
          config->width  = shared->screen_size.w;

     if (dfb_config->mode.height)
          config->height = dfb_config->mode.height;
     else
          config->height = shared->screen_size.h;

     if (dfb_config->mode.format != DSPF_UNKNOWN)
          config->pixelformat = dfb_config->mode.format;
     else if (dfb_config->mode.depth > 0)
          config->pixelformat = dfb_pixelformat_for_depth( dfb_config->mode.depth );
     else {
          int depth = DefaultDepthOfScreen( x11->screenptr );

          switch (depth) {
               case 15:
                    config->pixelformat = DSPF_RGB555;
                    break;
               case 16:
                    config->pixelformat = DSPF_RGB16;
                    break;
               case 24:
                    config->pixelformat = DSPF_RGB32;
                    break;
               case 32:
                    config->pixelformat = DSPF_ARGB;
                    break;
               default:
                    printf(" Unsupported X11 screen depth %d \n",depth);
                    return DFB_UNSUPPORTED;
          }
     }

     return DFB_OK;
}

static DFBResult
primaryTestRegion( CoreLayer                  *layer,
                   void                       *driver_data,
                   void                       *layer_data,
                   CoreLayerRegionConfig      *config,
                   CoreLayerRegionConfigFlags *failed )
{
     CoreLayerRegionConfigFlags  fail = 0;
     X11LayerData               *lds  = layer_data;

     D_DEBUG_AT( X11_Layer, "%s()\n", __FUNCTION__ );

     switch (config->buffermode) {
          case DLBM_FRONTONLY:
          case DLBM_BACKSYSTEM:
          case DLBM_BACKVIDEO:
          case DLBM_TRIPLE:
               break;

          default:
               fail |= CLRCF_BUFFERMODE;
               break;
     }

     switch (config->format) {
          case DSPF_LUT8:
          case DSPF_RGB16:
          case DSPF_NV16:
          case DSPF_RGB444:
          case DSPF_ARGB4444:
          case DSPF_RGBA4444:
          case DSPF_RGB555:
          case DSPF_ARGB1555:
          case DSPF_RGBA5551:
          case DSPF_BGR555:
          case DSPF_RGB24:
          case DSPF_RGB32:
          case DSPF_ARGB:
          case DSPF_ABGR:
          case DSPF_AYUV:
          case DSPF_AVYU:
          case DSPF_VYU:
          case DSPF_UYVY:
          case DSPF_ARGB8565:
          case DSPF_RGBAF88871:
          case DSPF_YUV444P:
          case DSPF_YV16:
               break;

          default:
               fail |= CLRCF_FORMAT;
               break;
     }

     if (config->options & ~(DLOP_ALPHACHANNEL | DLOP_LR_MONO | DLOP_STEREO | DLOP_OPACITY))
          fail |= CLRCF_OPTIONS;

     if (failed)
          *failed = fail;

     if (fail) {
          D_INFO("failed flags 0x%08x\n", fail);
          return DFB_UNSUPPORTED;
     }

     return DFB_OK;
}

static DFBResult
primaryAddRegion( CoreLayer             *layer,
                  void                  *driver_data,
                  void                  *layer_data,
                  void                  *region_data,
                  CoreLayerRegionConfig *config )
{
     DFBResult     ret;
     DFBX11       *x11 = driver_data;
     X11LayerData *lds = layer_data;

     D_DEBUG_AT( X11_Layer, "%s( lds %p )\n", __FUNCTION__, lds );

     CoreSurfaceConfig sconfig;

     sconfig.flags  = CSCONF_SIZE | CSCONF_FORMAT | CSCONF_CAPS;
     sconfig.size.w = config->width;
     sconfig.size.h = config->height;
     sconfig.format = config->format;
     sconfig.caps   = DSCAPS_PREMULTIPLIED;

     ret = dfb_surface_create( layer->core, &sconfig, CSTF_LAYER, dfb_layer_id(layer),
                               NULL, &lds->surface );
     if (ret)
          return ret;

     dfb_surface_globalize( lds->surface );

     return DFB_OK;
}

static DFBResult
primarySetRegion( CoreLayer                  *layer,
                  void                       *driver_data,
                  void                       *layer_data,
                  void                       *region_data,
                  CoreLayerRegionConfig      *config,
                  CoreLayerRegionConfigFlags  updated,
                  CoreSurface                *surface,
                  CorePalette                *palette,
                  CoreSurfaceBufferLock      *left_lock,
                  CoreSurfaceBufferLock      *right_lock )
{
     DFBResult  ret;

     DFBX11       *x11 = driver_data;
     X11LayerData *lds = layer_data;

     D_DEBUG_AT( X11_Layer, "%s( lds %p )\n", __FUNCTION__, lds );

     x11->Sync( x11 );
//     if (x11->shared->x_error)
//          return DFB_FAILURE;

//     if (lds->lock_left.allocation)
//          dfb_surface_allocation_unref( lds->lock_left.allocation );
//
//     if (lds->lock_right.allocation)
//          dfb_surface_allocation_unref( lds->lock_right.allocation );
//
//     if (lds->lock_left.buffer)
//          dfb_surface_buffer_unref( lds->lock_left.buffer );
//
//     if (lds->lock_right.buffer)
//          dfb_surface_buffer_unref( lds->lock_right.buffer );
//
//     memset( &lds->lock_left, 0, sizeof(lds->lock_left) );
//     memset( &lds->lock_right, 0, sizeof(lds->lock_right) );

     D_DEBUG_AT( X11_Layer, "  -> surface: %s\n", ToString_CoreSurface( surface ) );

//     ret = dfb_surface_ref( surface );
//     if (ret)
//          return ret;

     //if (lds->surface)
     //     dfb_surface_unref( lds->surface );

//     lds->surface = surface;

     lds->reconfig    = true;
     lds->new_config  = *config;

     D_DEBUG_AT( X11_Layer, "  -> config: %s\n", ToString_CoreLayerRegionConfig( config ) );
     D_DEBUG_AT( X11_Layer, "  -> source: "DFB_RECT_FORMAT"\n", DFB_RECTANGLE_VALS( &config->source ) );


     // FIXME: update with FlipUpdate
     if (palette)
          dfb_x11_set_palette( x11, lds, palette );


     x11->Sync( x11 );


     return DFB_OK;
}

static DFBResult
primarySetStereoDepth( CoreLayer              *layer,
                       void                   *driver_data,
                       void                   *layer_data,
                       bool                    follow_video,
                       int                     z )
{
     return DFB_OK;
}

static DFBResult
primaryRemoveRegion( CoreLayer             *layer,
                     void                  *driver_data,
                     void                  *layer_data,
                     void                  *region_data )
{
     DFBX11       *x11 = driver_data;
     X11LayerData *lds = layer_data;

     D_DEBUG_AT( X11_Layer, "%s()\n", __FUNCTION__ );

     //if (lds->surface) {
     //     dfb_surface_unref( lds->surface );
     //     lds->surface = NULL;
     //}

//     if (lds->lock_left.allocation)
//          dfb_surface_allocation_unref( lds->lock_left.allocation );
//
//     if (lds->lock_right.allocation)
//          dfb_surface_allocation_unref( lds->lock_right.allocation );
//
//     if (lds->lock_left.buffer)
//          dfb_surface_buffer_unref( lds->lock_left.buffer );
//
//     if (lds->lock_right.buffer)
//          dfb_surface_buffer_unref( lds->lock_right.buffer );
//
//     memset( &lds->lock_left, 0, sizeof(lds->lock_left) );
//     memset( &lds->lock_right, 0, sizeof(lds->lock_right) );

     //if (x11->shared->x_error)
     //     return DFB_FAILURE;

//     dfb_x11_destroy_window( x11, lds );

     dfb_surface_unlink( &lds->surface );

     return DFB_OK;
}

static DFBResult
primaryFlipUpdate( CoreLayer             *layer,
                   void                  *driver_data,
                   void                  *layer_data,
                   void                  *region_data,
                   CoreSurface           *surface,
                   const DFBRegion       *left_update,
                   CoreSurfaceBufferLock *left_lock,
                   const DFBRegion       *right_update,
                   CoreSurfaceBufferLock *right_lock,
                   bool                   flip )
{
     DFBX11       *x11 = driver_data;
     X11LayerData *lds = layer_data;

     DFBRegion  left_region  = DFB_REGION_INIT_FROM_DIMENSION( &surface->config.size );
     DFBRegion  right_region = DFB_REGION_INIT_FROM_DIMENSION( &surface->config.size );

     D_DEBUG_AT( X11_Layer, "%s( lds %p )\n", __FUNCTION__, lds );

     if (lds->reconfig) {
          lds->reconfig = false;
          lds->config   = lds->new_config;

          x11->shared->stereo       = !!(lds->config.options & DLOP_STEREO);
          x11->shared->stereo_width = lds->config.width / 2;

          if (lds->surface->config.size.w != lds->config.width  ||
              lds->surface->config.size.h != lds->config.height ||
              lds->surface->config.format != lds->config.format)
          {
               CoreSurfaceConfig sc;

               sc.flags  = CSCONF_SIZE | CSCONF_FORMAT;
               sc.size.w = lds->config.width;
               sc.size.h = lds->config.height;
               sc.format = lds->config.format;

               dfb_surface_reconfig( lds->surface, &sc );
          }
     }

     if (left_update)
          DFB_REGIONS_DEBUG_AT( X11_Layer, left_update, 1 );

     if (right_update)
          DFB_REGIONS_DEBUG_AT( X11_Layer, right_update, 1 );

     //if (x11->shared->x_error)
     //     return DFB_FAILURE;

     if (left_update && !dfb_region_region_intersect( &left_region, left_update ))
          return DFB_OK;

     if (right_update && !dfb_region_region_intersect( &right_region, right_update ))
          return DFB_OK;

     if (flip)
          dfb_surface_flip( surface, false );

//     if (lds->lock_left.allocation)
//          dfb_surface_allocation_unref( lds->lock_left.allocation );
//
//     if (lds->lock_right.allocation)
//          dfb_surface_allocation_unref( lds->lock_right.allocation );
//
//     if (lds->lock_left.buffer)
//          dfb_surface_buffer_unref( lds->lock_left.buffer );
//
//     if (lds->lock_right.buffer)
//          dfb_surface_buffer_unref( lds->lock_right.buffer );
//
//
//     lds->lock_left = *left_lock;
//
//     if (lds->config.options & DLOP_STEREO)
//          lds->lock_right = *right_lock;
//     else
//          memset( &lds->lock_right, 0, sizeof(lds->lock_right) );
//
//
//     D_DEBUG_AT( X11_Layer, "  -> left: %s\n", ToString_CoreSurfaceAllocation( lds->lock_left.allocation ) );
//
//
//     dfb_surface_allocation_ref( lds->lock_left.allocation );
//
//     if (lds->lock_right.allocation)
//          dfb_surface_allocation_ref( lds->lock_right.allocation );
//
//
//     dfb_surface_buffer_ref( lds->lock_left.buffer );
//
//     if (lds->lock_right.buffer)
//          dfb_surface_buffer_ref( lds->lock_right.buffer );

     x11->Sync( x11 );


     dfb_gfx_copy_to( surface, lds->surface, NULL, 0, 0, false );


     left_region.x1 = 0;
     left_region.y1 = 0;
     left_region.x2 = 1280-1;
     left_region.y2 = 720-1;
     dfb_x11_update_screen( x11, lds, &left_region, &right_region, left_lock, right_lock );

     x11->Sync( x11 );


     dfb_surface_notify_display2( surface, left_lock->allocation->index, left_lock->task );

     if (right_lock && right_lock->allocation)
          dfb_surface_notify_display2( surface, right_lock->allocation->index, right_lock->task );


//     if (lds->lock_left.task)
//          Task_Done( lds->lock_left.task );
//
//     if (lds->lock_right.task)
//          Task_Done( lds->lock_right.task );

     x11->Sync( x11 );

     return DFB_OK;
}

static DFBResult
primaryFlipRegion( CoreLayer             *layer,
                   void                  *driver_data,
                   void                  *layer_data,
                   void                  *region_data,
                   CoreSurface           *surface,
                   DFBSurfaceFlipFlags    flags,
                   const DFBRegion       *left_update,
                   CoreSurfaceBufferLock *left_lock,
                   const DFBRegion       *right_update,
                   CoreSurfaceBufferLock *right_lock )
{
     return primaryFlipUpdate( layer, driver_data, layer_data, region_data, surface, left_update, left_lock, right_update, right_lock, true );
}

static DFBResult
primaryUpdateRegion( CoreLayer             *layer,
                     void                  *driver_data,
                     void                  *layer_data,
                     void                  *region_data,
                     CoreSurface           *surface,
                     const DFBRegion       *left_update,
                     CoreSurfaceBufferLock *left_lock,
                     const DFBRegion       *right_update,
                     CoreSurfaceBufferLock *right_lock )
{
     return primaryFlipUpdate( layer, driver_data, layer_data, region_data, surface, left_update, left_lock, right_update, right_lock, false );
}

static DisplayLayerFuncs primaryLayerFuncs = {
     .LayerDataSize  = primaryLayerDataSize,
     .RegionDataSize = primaryRegionDataSize,
     .InitLayer      = primaryInitLayer,

     .TestRegion     = primaryTestRegion,
     .AddRegion      = primaryAddRegion,
     .SetRegion      = primarySetRegion,
     .SetStereoDepth = primarySetStereoDepth,
     .RemoveRegion   = primaryRemoveRegion,
     .FlipRegion     = primaryFlipRegion,
     .UpdateRegion   = primaryUpdateRegion,
};

DisplayLayerFuncs *x11PrimaryLayerFuncs = &primaryLayerFuncs;

/******************************************************************************/

static DFBResult
update_screen( DFBX11 *x11, const DFBRectangle *clip, CoreSurfaceBufferLock *lock, XWindow *xw )
{
     unsigned int           offset = 0;
     XImage                *ximage;
     CoreSurfaceAllocation *allocation;
     DFBX11Shared          *shared;
     DFBRectangle           rect;
     bool                   direct = false;
     CoreSurfaceBufferLock  lock_;
     X11Output             *output;

     D_ASSERT( x11 != NULL );
     DFB_RECTANGLE_ASSERT( clip );

     if (!xw)
          return DFB_OK;

     x11->Sync( x11 );

     D_DEBUG_AT( X11_Update, "%s( %4d,%4d-%4dx%4d )\n", __FUNCTION__, DFB_RECTANGLE_VALS( clip ) );

     CORE_SURFACE_BUFFER_LOCK_ASSERT( lock );

     shared = x11->shared;
     D_ASSERT( shared != NULL );

     output = &shared->output[xw->layer_id/shared->layers];

     allocation = lock->allocation;
     CORE_SURFACE_ALLOCATION_ASSERT( allocation );

     x11->Sync( x11 );
#if 1
     /* Check for Window allocation... */
     if (allocation->pool == shared->x11window_pool && lock->handle) {
          x11AllocationData *alloc = allocation->data;

          Window window = alloc->window;

          GC gc;

          XLockDisplay( x11->display );

          if (!window) {
               D_ASSUME( alloc->type == X11_ALLOC_PIXMAP );

               static Window w;
               static int ww, wh;

               if (!w
                   || ww != allocation->config.size.w
                   || wh != allocation->config.size.h)
               {
                    x11->Sync( x11 );

                    D_LOG( X11_Update, VERBOSE, "  -> Creating window %dx%d...\n", allocation->config.size.w, allocation->config.size.h );

                    if (w) {
                         D_LOG( X11_Update, VERBOSE, "     -> Destroying old window 0x%08lx (%dx%d)...\n", w, ww, wh );
                         XDestroyWindow( x11->display, w );
                         if (x11->showing == w)
                              x11->showing = 0;
                         w = 0;
                    }

                    ww = allocation->config.size.w;
                    wh = allocation->config.size.h;


                    XSetWindowAttributes attr;

                    attr.event_mask =
                           ButtonPressMask
                         | ButtonReleaseMask
                         | PointerMotionMask
                         | KeyPressMask
                         | KeyReleaseMask
                         | ExposureMask
                         | StructureNotifyMask;

                    attr.background_pixmap = 0;
//                    attr.override_redirect = True;

                    w = XCreateWindow( x11->display,
                                       DefaultRootWindow(x11->display),
                                       0, 0, ww, wh, 0,
                                       DefaultDepthOfScreen( DefaultScreenOfDisplay( x11->display ) ), InputOutput,
                                       DefaultVisualOfScreen( DefaultScreenOfDisplay( x11->display ) ), CWEventMask /*| CWOverrideRedirect*/, &attr );
                    x11->Sync( x11 );
                    D_DEBUG_AT( X11_Update, "  -> window 0x%08lx\n", (long) w );

//                    gc = XCreateGC(x11->display, w, 0, NULL);

                    XMapRaised( x11->display, w );
                    x11->Sync( x11 );
                    D_DEBUG_AT( X11_Update, "  -> raised\n" );
               }

               window = w;
          }

          gc = XCreateGC( x11->display, window, 0, NULL );

          if (x11->showing != window) {
               if (x11->showing) {
                    D_LOG( X11_Update, VERBOSE, "  -> Hiding previous window 0x%08lx...\n", x11->showing );
                    XUnmapWindow( x11->display, x11->showing );
                    x11->showing = 0;
               }


               x11->Sync( x11 );

               D_LOG( X11_Update, VERBOSE, "  -> Showing window 0x%08lx...\n", window );
               shared->x_error = 0;
               XMapRaised( x11->display, window );
               x11->Sync( x11 );

               if (shared->x_error)
                    shared->x_error = 0;
               else {
                    x11->showing = window;

                    x11->showing_w = lock->allocation->config.size.w;
                    x11->showing_h = lock->allocation->config.size.h;
               }
          }
          else
               D_LOG( X11_Update, DEBUG, "  -> Already showing window 0x%08lx\n", window );

          if (!alloc->window) {
               x11->Sync( x11 );

               if (alloc->depth == 32) {
                    D_DEBUG_AT( X11_Update, "  -> Copying via Image from Window/Pixmap...\n" );

                    XImage            *image;
                    XImage            *image2;

                    image = XGetImage( x11->display, alloc->window ? alloc->window : alloc->xid,
                                       0, 0, x11->showing_w, x11->showing_h, ~0, ZPixmap );


                    image2 = XCreateImage( x11->display,
                                           DefaultVisualOfScreen( DefaultScreenOfDisplay( x11->display ) ),
                                           DefaultDepthOfScreen( DefaultScreenOfDisplay( x11->display ) ),
                                           ZPixmap, 0,
                                           (void*) image->data, x11->showing_w, x11->showing_h, 32, x11->showing_w * 4 );
                    if (!image2) {
                         D_ERROR( "X11/Surfaces: XCreateImage( %dx%d, depth %d ) failed!\n", x11->showing_w, x11->showing_h, 24 );
                         XUnlockDisplay( x11->display );
                         return DFB_FAILURE;
                    }


                    XPutImage( x11->display, window, gc, image2, 0, 0, 0, 0, x11->showing_w, x11->showing_h );

                    XFlush( x11->display );

                    XDestroyImage( image );

                    image2->data = NULL;
                    XDestroyImage( image2 );
               }
               else {
                    D_DEBUG_AT( X11_Update, "  -> Copying from Window/Pixmap...\n" );

                    XCopyArea( x11->display, alloc->xid, window, gc,
                               0, 0, x11->showing_w, x11->showing_h, 0, 0 );
                    XFlush( x11->display );
               }

               x11->Sync( x11 );
          }

          XFreeGC( x11->display, gc );

          XUnlockDisplay( x11->display );

          return DFB_OK;
     }
#endif
     if (!xw)
          return DFB_OK;


     rect.x = rect.y = 0;
     rect.w = xw->width;
     rect.h = xw->height;

//     if (!dfb_rectangle_intersect( &rect, clip ))
//          return DFB_OK;

     D_DEBUG_AT( X11_Update, "  -> %4d,%4d-%4dx%4d\n", DFB_RECTANGLE_VALS( &rect ) );
#if 0
     /* Check for Pixmap allocation... */
     if (allocation->pool == shared->x11window_pool && lock->handle && lock->offset == X11_ALLOC_PIXMAP) {
          Pixmap pixmap = (long) lock->handle;

          D_DEBUG_AT( X11_Update, "  -> Copying from Pixmap...\n" );

          XLockDisplay( x11->display );

          //XCopyArea( x11->display, pixmap, xw->window, xw->gc,
          //           rect.x, rect.y, rect.w, rect.h, rect.x, rect.y );
          XCopyArea( x11->display, pixmap, xw->window, xw->gc,
                     rect.x, rect.y, rect.w, rect.h, rect.x, rect.y );

          XUnlockDisplay( x11->display );

          return DFB_OK;
     }
#endif

#ifdef USE_GLX__
     /* Check for GLX allocation... */
     if (allocation->pool == shared->glx_pool && lock->handle) {
          LocalPixmap *pixmap = lock->handle;

          D_MAGIC_ASSERT( pixmap, LocalPixmap );

          /* ...and just call SwapBuffers... */
          //D_DEBUG_AT( X11_Update, "  -> Calling glXSwapBuffers( 0x%lx )...\n", alloc->drawable );
          //glXSwapBuffers( x11->display, alloc->drawable );


          XLockDisplay( x11->display );

          D_DEBUG_AT( X11_Update, "  -> Copying from GLXPixmap...\n" );

          glXWaitGL();

          XCopyArea( x11->display, pixmap->pixmap, xw->window, xw->gc,
                     rect.x, rect.y, rect.w, rect.h, rect.x, rect.y );

          glXWaitX();

          XUnlockDisplay( x11->display );

          return DFB_OK;
     }
#endif

     /* Check for our special native allocation... */
#if 0
     if (allocation->pool == shared->x11image_pool && lock->handle) {
          x11Image *image = lock->handle;

          D_MAGIC_ASSERT( image, x11Image );

          /* ...and directly XShmPutImage from that. */
          ximage = image->ximage;

          direct = true;
     }
     else
#endif
     {
          DFBColor black = { 0, 0, 0, 0 };
          DFBColor red   = { 0xff, 0xff, 0, 0 };

          D_FLAGS_SET( output->gfx_state.modified, SMF_DESTINATION | SMF_CLIP );

          output->gfx_state.destination = output->surface;

          dfb_region_from_rectangle( &output->gfx_state.clip, &rect );

          dfb_state_set_color( &output->gfx_state, &black );

          CoreGraphicsStateClient_FillRectangles( &output->client, &rect, 1 );

          /* ...or copy or convert into XShmImage or XImage allocated with the XWindow. */
          ximage = xw->ximage;
          offset = xw->ximage_offset;

          for (int i=0; i<shared->layers; i++) {
               X11LayerData *ld = shared->output[xw->layer_id/shared->layers].layers[i];

               D_DEBUG_AT( X11_Layer, "  -> ld %p\n", ld );

               if (ld->surface/* && ld->lock_left.allocation*/) {
                    D_DEBUG_AT( X11_Layer, "  ->   surface    %s\n", ToString_CoreSurface( ld->surface ) );
//                    D_DEBUG_AT( X11_Layer, "  ->   buffer     %s\n", ToString_CoreSurfaceBuffer( ld->lock_left.buffer ) );
//                    D_DEBUG_AT( X11_Layer, "  ->   allocation %s\n", ToString_CoreSurfaceAllocation( ld->lock_left.allocation ) );

                    //if (ld->reconfig) {
                    //     output->gfx_state.destination = NULL;
                    //     output->gfx_state.source      = NULL;
                    //     return DFB_OK;
                    //}

///                    if (0 && allocation->pool == shared->x11window_pool && lock->handle) {
///                         x11AllocationData *alloc = allocation->data;
///
///                         D_DEBUG_AT( X11_Update, "  -> Copying from Window/Pixmap...\n" );
///
///                         D_ASSERT( shared->output[xw->layer_id/shared->layers].xw != NULL );
///
///                         XLockDisplay( x11->display );
///
///                         if (alloc->depth == 32) {
///                              D_DEBUG_AT( X11_Update, "  -> Copying via Image from Window/Pixmap...\n" );
///
///                              XImage            *image;
///                              XImage            *image2;
///
///                              image = XGetImage( x11->display, alloc->window ? alloc->window : alloc->xid,
///                                                 0, 0, x11->showing_w, x11->showing_h, ~0, ZPixmap );
///
///
///                              image2 = XCreateImage( x11->display,
///                                                     DefaultVisualOfScreen( DefaultScreenOfDisplay( x11->display ) ),
///                                                     DefaultDepthOfScreen( DefaultScreenOfDisplay( x11->display ) ),
///                                                     ZPixmap, 0,
///                                                     (void*) image->data, x11->showing_w, x11->showing_h, 32, x11->showing_w * 4 );
///                              if (!image2) {
///                                   D_ERROR( "X11/Surfaces: XCreateImage( %dx%d, depth %d ) failed!\n", x11->showing_w, x11->showing_h, 24 );
///                                   XUnlockDisplay( x11->display );
///                                   return DFB_FAILURE;
///                              }
///
///
///                              XPutImage( x11->display,
///                                         shared->output[xw->layer_id/shared->layers].xw->window,
///                                         shared->output[xw->layer_id/shared->layers].xw->gc,
///                                         image2, 0, 0, 0, 0, x11->showing_w, x11->showing_h );
///
///                              XDestroyImage( image );
///
///                              image2->data = NULL;
///                              XDestroyImage( image2 );
///                         }
///                         else {
///                              D_DEBUG_AT( X11_Update, "  -> Copying from Window/Pixmap...\n" );
///
///                              XCopyArea( x11->display, alloc->xid,
///                                         shared->output[xw->layer_id/shared->layers].xw->window,
///                                         shared->output[xw->layer_id/shared->layers].xw->gc,
///                                         0, 0, x11->showing_w, x11->showing_h, 0, 0 );
///                         }
///
///                         XFlush( x11->display );
///
///                         XUnlockDisplay( x11->display );
///                    }
///                    else {
                         D_FLAGS_SET( output->gfx_state.modified, SMF_SOURCE | SMF_FROM | SMF_TO | SMF_BLITTING_FLAGS );

                         output->gfx_state.source                 = ld->surface;
//                         output->gfx_state.source_buffer          = ld->lock_left.buffer;
//                         output->gfx_state.source_flip_count      = ld->lock_left.allocation->index;
//                         output->gfx_state.source_flip_count_used = true;
                         output->gfx_state.from                   = CSBR_FRONT;
                         output->gfx_state.from_eye               = DSSE_LEFT;
                         output->gfx_state.to                     = CSBR_BACK;
                         output->gfx_state.to_eye                 = DSSE_LEFT;
                         output->gfx_state.blittingflags          = DSBLIT_NOFX;

                         if (ld->config.options & DLOP_ALPHACHANNEL)
                              output->gfx_state.blittingflags = DSBLIT_BLEND_ALPHACHANNEL;

                         DFBRectangle rect = ld->config.source;
//                         DFBRectangle clip = { 0, 0, ld->lock_left.buffer->config.size.w, ld->lock_left.buffer->config.size.h };
//                         dfb_rectangle_intersect( &rect, &clip );
                         CoreGraphicsStateClient_StretchBlit( &output->client, &rect, &ld->config.dest, 1 );


                         dfb_state_set_color( &output->gfx_state, &red );

                         CoreGraphicsStateClient_DrawRectangles( &output->client, &ld->config.dest, 1 );
///                    }
               }
          }

          CoreGraphicsStateClient_Flush( &output->client, 0, CGSCFF_NONE );

          output->gfx_state.destination = NULL;
          output->gfx_state.source      = NULL;
     }

     D_ASSERT( ximage != NULL );


     XLockDisplay( x11->display );

     /* Wait for previous data to be processed... */
     x11->Sync( x11 );

     dfb_surface_lock_buffer( output->surface, CSBR_FRONT, CSAID_CPU, CSAF_WRITE, &lock_ );

     char *old_ptr = ximage->data;
     ximage->data = lock_.addr;

     /* ...and immediately queue or send the next! */
     if (x11->use_shm) {
          /* Just queue the command, it's XShm :) */
          XShmPutImage( xw->display, xw->window, xw->gc, ximage,
                        rect.x, rect.y + offset, rect.x, rect.y, rect.w, rect.h, False );

          /* Make sure the queue has really happened! */
          XFlush( x11->display );
     }
     else
          /* Initiate transfer of buffer... */
          XPutImage( xw->display, xw->window, xw->gc, ximage,
                     rect.x, rect.y + offset, rect.x, rect.y, rect.w, rect.h );

     dfb_surface_unlock_buffer( output->surface, &lock_ );

     ximage->data = old_ptr;

     /* Wait for display if single buffered and not converted... */
     if (direct && !(output->surface->config.caps & DSCAPS_FLIPPING))
          x11->Sync( x11 );

     XUnlockDisplay( x11->display );

     return DFB_OK;
}

static void
update_scaled565( XWindow *xw, const DFBRectangle *clip, CoreSurfaceBufferLock *lock, int xoffset )
{
     u32 *dst;
     u32 *src;
     int  x, y;

     D_ASSERT( xw != NULL );
     DFB_RECTANGLE_ASSERT( clip );

     D_DEBUG_AT( X11_Update, "%s( %4d,%4d-%4dx%4d )\n", __FUNCTION__, DFB_RECTANGLE_VALS( clip ) );

     CORE_SURFACE_BUFFER_LOCK_ASSERT( lock );

     dst = (u32*)(xw->virtualscreen + ((clip->x / 2) + xoffset) * xw->bpp + (clip->y + xw->ximage_offset) * xw->ximage->bytes_per_line);
     src = lock->addr + 2 * clip->x + clip->y * lock->pitch;

     for (y=0; y<clip->h; y++) {
          for (x=0; x<clip->w/2; x++) {
               u32 S2 = src[x];
               u16 result;

               S2 &= ~0x08210821;
               S2 >>= 1;

               result = (S2 & 0xffff) + (S2 >> 16);

               dst[x] = RGB16_TO_RGB32( result );
          }

          dst = (u32*)((u8*) dst + xw->ximage->bytes_per_line);
          src = (u32*)((u8*) src + lock->pitch);
     }
}

static void
update_scaled32( XWindow *xw, const DFBRectangle *clip, CoreSurfaceBufferLock *lock, int xoffset )
{
     u32 *dst;
     u64 *src;
     int  x, y;

     D_ASSERT( xw != NULL );
     DFB_RECTANGLE_ASSERT( clip );

     D_DEBUG_AT( X11_Update, "%s( %4d,%4d-%4dx%4d )\n", __FUNCTION__, DFB_RECTANGLE_VALS( clip ) );

     CORE_SURFACE_BUFFER_LOCK_ASSERT( lock );

     dst = (u32*)(xw->virtualscreen + ((clip->x / 2) + xoffset) * xw->bpp + (clip->y + xw->ximage_offset) * xw->ximage->bytes_per_line);
     src = lock->addr + 4 * clip->x + clip->y * lock->pitch;

     for (y=0; y<clip->h; y++) {
          for (x=0; x<clip->w/2; x++) {
               u64 S2 = src[x];

               S2 &= ~0x0101010101010101ULL;
               S2 >>= 1;

               dst[x] = ((u32) S2) + ((u32) (S2 >> 32));
          }

          dst = (u32*)((u8*) dst + xw->ximage->bytes_per_line);
          src = (u64*)((u8*) src + lock->pitch);
     }
}

static DFBResult
update_stereo( DFBX11 *x11, const DFBRectangle *left_clip, const DFBRectangle *right_clip,
               CoreSurfaceBufferLock *left_lock, CoreSurfaceBufferLock *right_lock, XWindow *xw )
{
     CoreSurface  *surface;
     DFBRectangle  left;
     DFBRectangle  right;

     D_ASSERT( x11 != NULL );
     DFB_RECTANGLE_ASSERT( left_clip );
     DFB_RECTANGLE_ASSERT( right_clip );

     D_DEBUG_AT( X11_Update, "%s( %4d,%4d-%4dx%4d | %4d,%4d-%4dx%4d )\n",
                 __FUNCTION__, DFB_RECTANGLE_VALS( left_clip ), DFB_RECTANGLE_VALS( right_clip ) );

     CORE_SURFACE_BUFFER_LOCK_ASSERT( left_lock );
     CORE_SURFACE_BUFFER_LOCK_ASSERT( right_lock );

     XLockDisplay( x11->display );

     if (!xw) {
          XUnlockDisplay( x11->display );
          return DFB_OK;
     }

     D_ASSERT( left_lock->allocation->surface == right_lock->allocation->surface );

     surface = left_lock->allocation->surface;
     D_ASSERT( surface != NULL );

     if (!left_lock->addr || !right_lock->addr)
          return DFB_UNSUPPORTED;

     xw->ximage_offset = (xw->ximage_offset ? 0 : xw->height);

     left  = *left_clip;
     right = *right_clip;

     if (left.x & 1) {
          left.x--;
          left.w++;
     }

     if (left.w & 1)
          left.w++;

     if (right.x & 1) {
          right.x--;
          right.w++;
     }

     if (right.w & 1)
          right.w++;

     switch (surface->config.format) {
          case DSPF_RGB16:
               update_scaled565( xw, &left, left_lock, 0 );
               update_scaled565( xw, &right, right_lock, xw->width / 2 );
               break;
          case DSPF_ARGB:
          case DSPF_RGB32:
               update_scaled32( xw, &left, left_lock, 0 );
               update_scaled32( xw, &right, right_lock, xw->width / 2 );
               break;
          default:
               return DFB_UNSUPPORTED;
     }


     left.x /= 2;
     left.w /= 2;

     right.x /= 2;
     right.w /= 2;

     right.x += xw->width/2;


     /* Wait for previous data to be processed... */
     x11->Sync( x11 );

     /* ...and immediately queue or send the next! */
     if (x11->use_shm) {
          /* Just queue the command, it's XShm :) */
          XShmPutImage( xw->display, xw->window, xw->gc, xw->ximage,
                        left.x, left.y + xw->ximage_offset, left.x, left.y, left.w, left.h, False );

          /* Just queue the command, it's XShm :) */
          XShmPutImage( xw->display, xw->window, xw->gc, xw->ximage,
                        right.x, right.y + xw->ximage_offset, right.x, right.y, right.w, right.h, False );

          /* Make sure the queue has really happened! */
          XFlush( x11->display );
     }
     else {
          /* Initiate transfer of buffer... */
          XPutImage( xw->display, xw->window, xw->gc, xw->ximage,
                     left.x, left.y + xw->ximage_offset, left.x, left.y, left.w, left.h );

          /* Initiate transfer of buffer... */
          XPutImage( xw->display, xw->window, xw->gc, xw->ximage,
                     right.x, right.y + xw->ximage_offset, right.x, right.y, right.w, right.h );
     }

     XUnlockDisplay( x11->display );

     return DFB_OK;
}

/******************************************************************************/

DFBResult
dfb_x11_create_window_handler( DFBX11 *x11, SetModeData *setmode )
{
     XWindow                *xw;
     DFBX11Shared           *shared = x11->shared;
     CoreLayerRegionConfig  *config;

     config = &setmode->config;
     xw     = *(setmode->xw);

     D_DEBUG_AT( X11_Layer, "%s( %p )\n", __FUNCTION__, config );

     D_DEBUG_AT( X11_Layer, "  -> %4dx%4d %s\n", config->width, config->height, dfb_pixelformat_name(config->format) );

     XLockDisplay( x11->display );

     if (xw != NULL) {
          if (xw->width == config->width && xw->height == config->height) {
               XUnlockDisplay( x11->display );
               return DFB_OK;
          }

          *(setmode->xw) = NULL;
          dfb_x11_close_window( x11, xw );
          shared->window_count--;
     }

     bool bSucces = dfb_x11_open_window( x11, &xw, dfb_config->x11_position.x, dfb_config->x11_position.y,
                                         shared->output[setmode->output].size.w, shared->output[setmode->output].size.h,
                                         config->format );

     xw->layer_id = setmode->layer_id;

     /* Set video mode */
     if ( !bSucces ) {
          D_ERROR( "DirectFB/X11: Couldn't open %dx%d window!\n", config->width, config->height );

          XUnlockDisplay( x11->display );
          return DFB_FAILURE;
     }
     else {
          *(setmode->xw) = xw;
          shared->window_count++;
     }

     XUnlockDisplay( x11->display );
     return DFB_OK;
}

DFBResult
dfb_x11_destroy_window_handler( DFBX11 *x11, DestroyData *destroy )
{
     DFBX11Shared *shared = x11->shared;
     XWindow      *xw;

     D_DEBUG_AT( X11_Layer, "%s()\n", __FUNCTION__ );

     XLockDisplay( x11->display );

     xw = *(destroy->xw);

     if (xw) {
          *(destroy->xw) = NULL;

          dfb_x11_close_window( x11, xw );
          shared->window_count--;
     }

     x11->Sync( x11 );

     XUnlockDisplay( x11->display );

     return DFB_OK;
}

DFBResult
dfb_x11_update_screen_handler( DFBX11 *x11, UpdateScreenData *data )
{
     D_DEBUG_AT( X11_Update, "%s( %p )\n", __FUNCTION__, data );

     if (data->stereo) {
          DFBRectangle left_rect;
          DFBRectangle right_rect;

          left_rect  = DFB_RECTANGLE_INIT_FROM_REGION( &data->left_region );
          right_rect = DFB_RECTANGLE_INIT_FROM_REGION( &data->right_region );

          if (data->left_lock.buffer && data->right_lock.buffer)
               update_stereo( x11, &left_rect, &right_rect, &data->left_lock, &data->right_lock, data->xw );
     }
     else {
          DFBRectangle rect;

          rect = DFB_RECTANGLE_INIT_FROM_REGION( &data->left_region );

          if (data->left_lock.buffer)
               update_screen( x11, &rect, &data->left_lock, data->xw );
     }

     data->left_lock.buffer  = NULL;
     data->right_lock.buffer = NULL;

     return DFB_OK;
}

DFBResult
dfb_x11_set_palette_handler( DFBX11 *x11, CorePalette *palette )
{
     return DFB_OK;
}

