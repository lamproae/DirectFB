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
