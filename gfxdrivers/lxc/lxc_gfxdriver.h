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
