#include <stdio.h>

#include <core/graphics_driver.h>
#include <core/surface_pool.h>
#include <core/screens.h>
#include <core/layers.h>

#include "lxc_gfxdriver.h"
#include "lxc_layer.h"
#include "lxc_screen.h"

DFB_GRAPHICS_DRIVER( lxc )

/******************************************************************************/
static int
driver_probe(
     CoreGraphicsDevice * device )
{
     return 1;
}

static void
driver_get_info(
     CoreGraphicsDevice * device,
     GraphicsDriverInfo * info )
{
     /*
      * fill driver info structure 
      */
     snprintf( info->name, DFB_GRAPHICS_DRIVER_INFO_NAME_LENGTH, "lxc Driver" );

     snprintf( info->vendor, DFB_GRAPHICS_DRIVER_INFO_VENDOR_LENGTH, "lxc" );

     info->version.major = 0;
     info->version.minor = 0;

     info->driver_data_size = sizeof( LXCDriverData );
     info->device_data_size = sizeof( LXCDeviceData );
}

static DFBResult
driver_init_driver(
     CoreGraphicsDevice * device,
     GraphicsDeviceFuncs * funcs,
     void *driver_data,
     void *device_data,
     CoreDFB * core )
{
     LXCDriverData *ddrv = driver_data;

     ddrv->ddev = device_data;

     /*
      * We don't provide any function pointers.
      * Everything is done through software
      */

     /*
      * register screen(s) and layer(s) 
      */
     ddrv->screen =
         dfb_screens_register( device, driver_data, &lxcScreenFuncs );
     ddrv->layer =
         dfb_layers_register( ddrv->screen, driver_data, &lxcLayerFuncs );

     return DFB_OK;
}

static DFBResult
driver_init_device(
     CoreGraphicsDevice * device,
     GraphicsDeviceInfo * device_info,
     void *driver_data,
     void *device_data )
{
     /*
      * fill device info 
      */
     snprintf( device_info->vendor, DFB_GRAPHICS_DEVICE_INFO_VENDOR_LENGTH,
               "lxc" );
     snprintf( device_info->name, DFB_GRAPHICS_DEVICE_INFO_NAME_LENGTH,
               "Blitter for LXC container" );

     /*
      * device limitations 
      */
     device_info->limits.surface_byteoffset_alignment = 8;
     device_info->limits.surface_bytepitch_alignment = 8;

     device_info->caps.flags = CCF_READSYSMEM | CCF_WRITESYSMEM;

     /*
      * no accelaration. Everything is done through software
      */
     device_info->caps.accel = DFXL_NONE;
     device_info->caps.drawing = DFXL_NONE;
     device_info->caps.blitting = DFXL_NONE;
     device_info->caps.clip = DFXL_NONE;

     dfb_surface_pool_gfx_driver_update( CSTF_ALL, CSAID_CPU,
                                         CSAF_READ | CSAF_WRITE | CSAF_SHARED );

     return DFB_OK;
}

static void
driver_close_device(
     CoreGraphicsDevice * device,
     void *driver_data,
     void *device_data )
{
}

static void
driver_close_driver(
     CoreGraphicsDevice * device,
     void *driver_data )
{
}
