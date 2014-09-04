/* 
 * (c) Copyright 2012-2013  DirectFB integrated media GmbH
 * (c) Copyright 2001-2013  The world wide DirectFB Open Source Community (directfb.org)
 * (c) Copyright 2000-2004  Convergence (integrated media) GmbH
 * 
 * All rights reserved.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef DFB_CLIENT_PROTOCOL_H
#define DFB_CLIENT_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-client.h"

struct wl_client;
struct wl_resource;

struct wl_dfb;
struct wl_dfb_buffer;

extern const struct wl_interface wl_dfb_interface;
extern const struct wl_interface wl_dfb_buffer_interface;

#ifndef WL_DFB_ERROR_ENUM
#define WL_DFB_ERROR_ENUM
enum wl_dfb_error {
	WL_DFB_ERROR_AUTHENTICATE_FAIL = 0,
	WL_DFB_ERROR_INVALID_FORMAT = 1,
	WL_DFB_ERROR_INVALID_NAME = 2,
};
#endif /* WL_DFB_ERROR_ENUM */

#define WL_DFB_CREATE_BUFFER	0

static inline void
wl_dfb_set_user_data(struct wl_dfb *wl_dfb, void *user_data)
{
	wl_proxy_set_user_data((struct wl_proxy *) wl_dfb, user_data);
}

static inline void *
wl_dfb_get_user_data(struct wl_dfb *wl_dfb)
{
	return wl_proxy_get_user_data((struct wl_proxy *) wl_dfb);
}

static inline void
wl_dfb_destroy(struct wl_dfb *wl_dfb)
{
	wl_proxy_destroy((struct wl_proxy *) wl_dfb);
}

static inline struct wl_dfb_buffer *
wl_dfb_create_buffer(struct wl_dfb *wl_dfb, uint32_t buffer_id)
{
	struct wl_proxy *id;

	id = wl_proxy_marshal_constructor((struct wl_proxy *) wl_dfb,
			 WL_DFB_CREATE_BUFFER, &wl_dfb_buffer_interface, NULL, buffer_id);

	return (struct wl_dfb_buffer *) id;
}

struct wl_dfb_buffer_listener {
	/**
	 * release - compositor releases buffer
	 *
	 * Sent when this wl_buffer is no longer used by the compositor.
	 * The client is now free to re-use or destroy this buffer and its
	 * backing storage.
	 *
	 * If a client receives a release event before the frame callback
	 * requested in the same wl_surface.commit that attaches this
	 * wl_buffer to a surface, then the client is immediately free to
	 * re-use the buffer and its backing storage, and does not need a
	 * second buffer for the next surface content update. Typically
	 * this is possible, when the compositor maintains a copy of the
	 * wl_surface contents, e.g. as a GL texture. This is an important
	 * optimization for GL(ES) compositors with wl_shm clients.
	 */
	void (*release)(void *data,
			struct wl_dfb_buffer *wl_dfb_buffer);
};

static inline int
wl_dfb_buffer_add_listener(struct wl_dfb_buffer *wl_dfb_buffer,
			   const struct wl_dfb_buffer_listener *listener, void *data)
{
	return wl_proxy_add_listener((struct wl_proxy *) wl_dfb_buffer,
				     (void (**)(void)) listener, data);
}

#define WL_DFB_BUFFER_DESTROY	0
#define WL_DFB_BUFFER_SET_DISPLAY_TIMESTAMP	1

static inline void
wl_dfb_buffer_set_user_data(struct wl_dfb_buffer *wl_dfb_buffer, void *user_data)
{
	wl_proxy_set_user_data((struct wl_proxy *) wl_dfb_buffer, user_data);
}

static inline void *
wl_dfb_buffer_get_user_data(struct wl_dfb_buffer *wl_dfb_buffer)
{
	return wl_proxy_get_user_data((struct wl_proxy *) wl_dfb_buffer);
}

static inline void
wl_dfb_buffer_destroy(struct wl_dfb_buffer *wl_dfb_buffer)
{
	wl_proxy_marshal((struct wl_proxy *) wl_dfb_buffer,
			 WL_DFB_BUFFER_DESTROY);

	wl_proxy_destroy((struct wl_proxy *) wl_dfb_buffer);
}

static inline void
wl_dfb_buffer_set_display_timestamp(struct wl_dfb_buffer *wl_dfb_buffer, uint32_t ts_low, uint32_t ts_high)
{
	wl_proxy_marshal((struct wl_proxy *) wl_dfb_buffer,
			 WL_DFB_BUFFER_SET_DISPLAY_TIMESTAMP, ts_low, ts_high);
}

#ifdef  __cplusplus
}
#endif

#endif
