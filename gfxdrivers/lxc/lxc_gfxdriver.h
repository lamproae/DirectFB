#ifndef __LXC_GFXDRIVER_H__
#define __LXC_GFXDRIVER_H__

#ifndef FB_ACCEL_LXC_BLITTER
#define FB_ACCEL_LXC_BLITTER 80
#endif

#include <direct/types.h>
#include <direct/processor.h>
#include <core/gfxcard.h>

#include <sys/socket.h>
#include <sys/un.h>

#define SOCK_PATH "/dev/directfb_notif"

typedef struct {
     /* validation flags */
     int v_flags;

     /* cached/computed values */
     void *dst_addr;
     unsigned long dst_phys;
     unsigned int dst_int;
     unsigned long dst_pitch;
     DFBSurfacePixelFormat dst_format;
     unsigned long dst_bpp;

     void *src_addr;
     unsigned long src_phys;
     unsigned long src_pitch;
     DFBSurfacePixelFormat src_format;
     unsigned long src_bpp;

     unsigned long color_pixel;

        /** Add shared data here... **/
} LXCDeviceData;

typedef struct {
#if 0
     DirectProcessor processor;
     unsigned int packet_count;

     DirectMutex wait_lock;
     DirectWaitQueue wait_queue;

     CoreGraphicsSerial last;
     CoreGraphicsSerial next;
     CoreGraphicsSerial done;

        /** Add local data here... **/
#endif
     LXCDeviceData *ddev;
     CoreDFB *core;
     CoreScreen *screen;
     CoreLayer *layer;

     LXCDeviceData *device_data;
} LXCDriverData;

#endif
