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

#ifndef __LXC_LAYER_H__
#define __LXC_LAYER_H__

#include <core/layers.h>

#define LXC_LAYER_SUPPORTED_OPTIONS  (DLOP_NONE)

/* Define the resolution of the screen. Make sure that it's the same
 * as define in /usr/local/etc/directfbrc
 */
#define XRESOLUTION 720
#define YRESOLUTION 576

typedef struct {
     int socket_to_host;
} LXCLayerData;

typedef struct {
     CoreLayerRegionConfig config;

     CoreSurface *surface;
     CorePalette *palette;
} LXCRegionData;

extern const DisplayLayerFuncs lxcLayerFuncs;

#endif                          /* __LXC_LAYER_H__ */
