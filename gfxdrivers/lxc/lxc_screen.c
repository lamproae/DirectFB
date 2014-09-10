/*
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

// #define DIRECT_ENABLE_DEBUG

#include <stdio.h>

#include "lxc_gfxdriver.h"
#include "lxc_layer.h"
#include "lxc_screen.h"

D_DEBUG_DOMAIN( lxc_Screen, "lxc/Screen", "LXC Screen" );

/******************************************************************************/

static DFBResult
lxcInitScreen(
     CoreScreen * screen,
     CoreGraphicsDevice * device,
     void *driver_data,
     void *screen_data,
     DFBScreenDescription * description )
{
     D_ASSERT( description != NULL );

     D_DEBUG_AT( lxc_Screen, "%s()\n", __FUNCTION__ );

     /*
      * Set the screen capabilities. 
      */
     description->caps = DSCCAPS_NONE;  /* no capabilities */

     /*
      * Set the screen name. 
      */
     snprintf( description->name, DFB_SCREEN_DESC_NAME_LENGTH,
               "Screen for LXC" );

     description->mixers = 0;   /* no mixers */
     description->encoders = 0; /* no encoders */
     description->outputs = 0;  /* no outputs */

     return DFB_OK;
}

static DFBResult
lxcGetScreenSize(
     CoreScreen * screen,
     void *driver_data,
     void *screen_data,
     int *ret_width,
     int *ret_height )
{
     D_ASSERT( ret_width != NULL );
     D_ASSERT( ret_height != NULL );

     D_DEBUG_AT( lxc_Screen, "%s()\n", __FUNCTION__ );

     *ret_width = XRESOLUTION;
     *ret_height = YRESOLUTION;

     return DFB_OK;
}

ScreenFuncs lxcScreenFuncs = {
     .InitScreen = lxcInitScreen,
     .GetScreenSize = lxcGetScreenSize,
};
