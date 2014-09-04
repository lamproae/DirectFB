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



#include <config.h>

#include <direct/Types++.h>

#include <directfb_util.h>


extern "C" {
#include <direct/debug.h>
#include <direct/mem.h>
#include <direct/messages.h>

#include <core/core.h>
#include <core/surface.h>
#include <core/surface_pool.h>

#include <gfx/util.h>
}

#include <core/CoreSurface.h>
#include <core/Debug.h>
#include <core/SurfaceTask.h>
#include <core/TaskManager.h>


D_DEBUG_DOMAIN( DirectFB_CoreSurface, "DirectFB/CoreSurface", "DirectFB CoreSurface" );

/*********************************************************************************************************************/

namespace DirectFB {



DFBResult
ISurface_Real::SetConfig(
                        const CoreSurfaceConfig                   *config
                        )
{
     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s()\n", __FUNCTION__ );

     D_ASSERT( config != NULL );

     return dfb_surface_reconfig( obj, config );
}


DFBResult
ISurface_Real::Flip(
                   DFBBoolean                                    swap
                   )
{
     DFBResult ret;

     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s()\n", __FUNCTION__ );

     dfb_surface_lock( obj );

     ret = dfb_surface_flip_buffers( obj, swap );

     dfb_surface_unlock( obj );

     return ret;
}


DFBResult
ISurface_Real::GetPalette(
                         CorePalette                              **ret_palette
                         )
{
     DFBResult ret;

     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s()\n", __FUNCTION__ );

     D_ASSERT( ret_palette != NULL );

     if (!obj->palette)
          return DFB_UNSUPPORTED;

     ret = (DFBResult) dfb_palette_ref( obj->palette );
     if (ret)
          return ret;

     *ret_palette = obj->palette;

     return DFB_OK;
}


DFBResult
ISurface_Real::SetPalette(
                         CorePalette                               *palette
                         )
{
     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s()\n", __FUNCTION__ );

     D_ASSERT( palette != NULL );

     return dfb_surface_set_palette( obj, palette );
}


DFBResult
ISurface_Real::SetAlphaRamp(
                         u8                                         a0,
                         u8                                         a1,
                         u8                                         a2,
                         u8                                         a3
                         )
{
     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s()\n", __FUNCTION__ );

     return dfb_surface_set_alpha_ramp( obj, a0, a1, a2, a3 );
}


DFBResult
ISurface_Real::SetField(
                         s32                                        field
                         )
{
     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s()\n", __FUNCTION__ );

     return dfb_surface_set_field( obj, field );
}


static void
manage_interlocks( CoreSurfaceAllocation  *allocation,
                   CoreSurfaceAccessorID   accessor,
                   CoreSurfaceAccessFlags  access )
{
     int locks;

     locks = dfb_surface_allocation_locks( allocation );

     /*
      * Manage access interlocks.
      *
      * SOON FIXME: Clearing flags only when not locked yet. Otherwise nested GPU/CPU locks are a problem.
      */
     /* Software read/write access... */
     if (accessor != CSAID_GPU) {
          /* If hardware has written or is writing... */
          if (allocation->accessed[CSAID_GPU] & CSAF_WRITE) {
               /* ...wait for the operation to finish. */
               dfb_gfxcard_wait_serial( &allocation->gfx_serial );

               /* Software read access after hardware write requires flush of the (bus) read cache. */
               dfb_gfxcard_flush_read_cache();

               if (!locks) {
                    /* ...clear hardware write access. */
                    allocation->accessed[CSAID_GPU] = (CoreSurfaceAccessFlags)(allocation->accessed[CSAID_GPU] & ~CSAF_WRITE);

                    /* ...clear hardware read access (to avoid syncing twice). */
                    allocation->accessed[CSAID_GPU] = (CoreSurfaceAccessFlags)(allocation->accessed[CSAID_GPU] & ~CSAF_READ);
               }
          }

          /* Software write access... */
          if (access & CSAF_WRITE) {
               /* ...if hardware has (to) read... */
               if (allocation->accessed[CSAID_GPU] & CSAF_READ) {
                    /* ...wait for the operation to finish. */
                    dfb_gfxcard_wait_serial( &allocation->gfx_serial );

                    /* ...clear hardware read access. */
                    if (!locks)
                         allocation->accessed[CSAID_GPU] = (CoreSurfaceAccessFlags)(allocation->accessed[CSAID_GPU] & ~CSAF_READ);
               }
          }
     }

     /* Hardware read or write access... */
     if (accessor == CSAID_GPU && access & (CSAF_READ | CSAF_WRITE)) {
          /* ...if software has read or written before... */
          /*
           * TODO: For graphics drivers that do not support internal surface usage,
           *       probably do NOT want to test the CSAF_WRITE flag in the following if
           *       condition if the allocation's associated pool does NOT have the GPU
           *       read/write flags set as supported.
           */
          if (allocation->accessed[CSAID_CPU] & (CSAF_READ | CSAF_WRITE)) {
               /* ...flush texture cache. */
               /* TODO: Optimize by providing "lock" to graphics driver flush function. */
               dfb_gfxcard_flush_texture_cache();

               /* ...clear software read and write access. */
               if (!locks)
                    allocation->accessed[CSAID_CPU] = (CoreSurfaceAccessFlags)(allocation->accessed[CSAID_CPU] & ~(CSAF_READ | CSAF_WRITE));
          }
     }

     if (! D_FLAGS_ARE_SET( allocation->accessed[accessor], access )) {
          /* FIXME: surface_enter */
     }

     /* Collect... */
     allocation->accessed[accessor] = (CoreSurfaceAccessFlags)(allocation->accessed[accessor] | access);
}

class LockTask : public SurfaceTask
{
public:
     LockTask()
          :
          SurfaceTask( CSAID_CPU ),
          pushed( false ),
          timeout( false )
     {
          direct_mutex_init( &lock );
          direct_waitqueue_init( &wq );
     }

     virtual ~LockTask()
     {
          direct_mutex_deinit( &lock );
          direct_waitqueue_deinit( &wq );
     }

     bool Wait()
     {
          direct_mutex_lock( &lock );

          while (!pushed) {
               if (direct_waitqueue_wait_timeout( &wq, &lock, 20000000 ) == DR_TIMEOUT) {
                    D_ERROR( "CoreSurface/LockTask: Timeout waiting for task!\n" );
                    DirectFB::TaskManager::dumpTasks();
                    timeout = true;
                    break;
               }
          }

          direct_mutex_unlock( &lock );

          return !timeout;
     }

protected:
     virtual DFBResult Push()
     {
          return Run();
     }

     virtual DFBResult Run()
     {
          direct_mutex_lock( &lock );

          pushed = true;

          direct_waitqueue_broadcast( &wq );

          if (timeout)
               Done();
          else
               /* Call SurfaceTask::CacheInvalidate() for cache invalidation,
                  FIXME: flush does not take place at the moment */
               CacheInvalidate();

          direct_mutex_unlock( &lock );

          return DFB_OK;
     }

private:
     DirectMutex     lock;
     DirectWaitQueue wq;
     bool            pushed;
     bool            timeout;
};




DFBResult
ISurface_Real::PreLockBuffer(
                         CoreSurfaceBuffer                         *buffer,
                         CoreSurfaceAccessorID                      accessor,
                         CoreSurfaceAccessFlags                     access,
                         CoreSurfaceAllocation                    **ret_allocation
                         )
{
     DFBResult              ret;
     CoreSurfaceAllocation *allocation;
     CoreSurface           *surface    = obj;
     bool                   allocated  = false;

     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s()\n", __FUNCTION__ );

     D_MAGIC_ASSERT( buffer, CoreSurfaceBuffer );

     D_ASSERT( !dfb_config->task_manager || accessor == CSAID_CPU );

     dfb_surface_lock( surface );

     if (surface->state & CSSF_DESTROYED) {
          dfb_surface_unlock( surface );
          return DFB_DESTROYED;
     }

     if (!buffer->surface) {
          dfb_surface_unlock( surface );
          return DFB_BUFFEREMPTY;
     }

     /* Look for allocation with proper access. */
     allocation = dfb_surface_buffer_find_allocation( buffer, accessor, access, true );
     if (!allocation) {
          /* If no allocation exists, create one. */
          ret = dfb_surface_pools_allocate( buffer, accessor, access, &allocation );
          if (ret) {
               if (ret != DFB_NOVIDEOMEMORY && ret != DFB_UNSUPPORTED)
                    D_DERROR( ret, "Core/SurfBuffer: Buffer allocation failed!\n" );

               goto out;
          }

          allocated = true;
     }

     CORE_SURFACE_ALLOCATION_ASSERT( allocation );

     /* Synchronize with other allocations. */
     ret = dfb_surface_allocation_update( allocation, access );
     if (ret) {
          /* Destroy if newly created. */
          if (allocated)
               dfb_surface_allocation_decouple( allocation );
          goto out;
     }


     if (dfb_config->task_manager) {
          D_ASSERT( accessor == CSAID_CPU );

          LockTask *task = new LockTask();

          task->AddAccess( allocation, access );

          task->Flush();

          if (task->Wait())
               task->Done();
     }
     else {
          ret = dfb_surface_pool_prelock( allocation->pool, allocation, accessor, access );
          if (ret) {
               /* Destroy if newly created. */
               if (allocated)
                    dfb_surface_allocation_decouple( allocation );
               goto out;
          }

          manage_interlocks( allocation, accessor, access );
     }

     dfb_surface_allocation_ref( allocation );

     *ret_allocation = allocation;

out:
     dfb_surface_unlock( surface );

     return ret;
}

DFBResult
ISurface_Real::PreLockBuffer2(
                         CoreSurfaceBufferRole                      role,
                         DFBSurfaceStereoEye                        eye,
                         CoreSurfaceAccessorID                      accessor,
                         CoreSurfaceAccessFlags                     access,
                         DFBBoolean                                 lock,
                         CoreSurfaceAllocation                    **ret_allocation
                         )
{
     DFBResult              ret;
     CoreSurfaceBuffer     *buffer;
     CoreSurfaceAllocation *allocation;
     CoreSurface           *surface    = obj;
     bool                   allocated  = false;

     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s( surface %p, role %d, eye %d, accessor 0x%02x, access 0x%02x, %slock )\n",
                 __FUNCTION__, surface, role, eye, accessor, access, lock ? "" : "no " );

     D_ASSERT( !dfb_config->task_manager || accessor == CSAID_CPU );

     ret = (DFBResult) dfb_surface_lock( surface );
     if (ret)
          return ret;

     if (surface->state & CSSF_DESTROYED) {
          dfb_surface_unlock( surface );
          return DFB_DESTROYED;
     }

     if (surface->num_buffers < 1) {
          dfb_surface_unlock( surface );
          return DFB_BUFFEREMPTY;
     }

     buffer = dfb_surface_get_buffer2( surface, role, eye );
     D_MAGIC_ASSERT( buffer, CoreSurfaceBuffer );

     D_DEBUG_AT( DirectFB_CoreSurface, "  -> buffer %p\n", buffer );

     if (!lock && access & CSAF_READ) {
          if (fusion_vector_is_empty( &buffer->allocs )) {
               dfb_surface_unlock( surface );
               return DFB_NOALLOCATION;
          }
     }

     /* Look for allocation with proper access. */
     allocation = dfb_surface_buffer_find_allocation( buffer, accessor, access, lock );
     if (!allocation) {
          /* If no allocation exists, create one. */
          ret = dfb_surface_pools_allocate( buffer, accessor, access, &allocation );
          if (ret) {
               if (ret != DFB_NOVIDEOMEMORY && ret != DFB_UNSUPPORTED)
                    D_DERROR( ret, "Core/SurfBuffer: Buffer allocation failed!\n" );

               goto out;
          }

          allocated = true;
     }

     CORE_SURFACE_ALLOCATION_ASSERT( allocation );

     /* Synchronize with other allocations. */
     ret = dfb_surface_allocation_update( allocation, access );
     if (ret) {
          /* Destroy if newly created. */
          if (allocated)
               dfb_surface_allocation_decouple( allocation );
          goto out;
     }

     if (!lock) {
          if (access & CSAF_WRITE) {
               if (!(allocation->pool->desc.caps & CSPCAPS_WRITE))
                    lock = DFB_TRUE;
          }
          else if (access & CSAF_READ) {
               if (!(allocation->pool->desc.caps & CSPCAPS_READ))
                    lock = DFB_TRUE;
          }
     }

     if (lock) {
          if (dfb_config->task_manager) {
               D_ASSERT( accessor == CSAID_CPU );

               LockTask *task = new LockTask();

               task->AddAccess( allocation, access );

               task->Flush();

               if (task->Wait())
                    task->Done();
          }
          else {
               ret = dfb_surface_pool_prelock( allocation->pool, allocation, accessor, access );
               if (ret) {
                    /* Destroy if newly created. */
                    if (allocated)
                         dfb_surface_allocation_decouple( allocation );
                    goto out;
               }

               manage_interlocks( allocation, accessor, access );
          }
     }

     dfb_surface_allocation_ref( allocation );

     *ret_allocation = allocation;

out:
     dfb_surface_unlock( surface );

     return ret;
}

DFBResult
ISurface_Real::PreReadBuffer(
                         CoreSurfaceBuffer                         *buffer,
                         const DFBRectangle                        *rect,
                         CoreSurfaceAllocation                    **ret_allocation
                         )
{
     DFBResult              ret;
     CoreSurfaceAllocation *allocation;
     CoreSurface           *surface    = obj;
     bool                   allocated  = false;

     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s()\n", __FUNCTION__ );

     D_MAGIC_ASSERT( buffer, CoreSurfaceBuffer );

     dfb_surface_lock( surface );

     if (surface->state & CSSF_DESTROYED) {
          dfb_surface_unlock( surface );
          return DFB_DESTROYED;
     }

     if (!buffer->surface) {
          dfb_surface_unlock( surface );
          return DFB_BUFFEREMPTY;
     }

     /* Use last written allocation if it's up to date... */
     if (buffer->written && direct_serial_check( &buffer->written->serial, &buffer->serial ))
          allocation = buffer->written;
     else {
          /* ...otherwise look for allocation with CPU access. */
          allocation = dfb_surface_buffer_find_allocation( buffer, CSAID_CPU, CSAF_READ, false );
          if (!allocation) {
               /* If no allocation exists, create one. */
               ret = dfb_surface_pools_allocate( buffer, CSAID_CPU, CSAF_READ, &allocation );
               if (ret) {
                    D_DERROR( ret, "Core/SurfBuffer: Buffer allocation failed!\n" );
                    goto out;
               }

               allocated = true;
          }
     }

     CORE_SURFACE_ALLOCATION_ASSERT( allocation );

     /* Synchronize with other allocations. */
     ret = dfb_surface_allocation_update( allocation, CSAF_READ );
     if (ret) {
          /* Destroy if newly created. */
          if (allocated)
               dfb_surface_allocation_decouple( allocation );
          goto out;
     }

     if (!(allocation->pool->desc.caps & CSPCAPS_READ)) {
          if (dfb_config->task_manager) {
               LockTask *task = new LockTask();

               task->AddAccess( allocation, CSAF_READ );

               task->Flush();

               if (task->Wait())
                    task->Done();
          }
          else {
               ret = dfb_surface_pool_prelock( allocation->pool, allocation, CSAID_CPU, CSAF_READ );
               if (ret) {
                    /* Destroy if newly created. */
                    if (allocated)
                         dfb_surface_allocation_decouple( allocation );
                    goto out;
               }

               manage_interlocks( allocation, CSAID_CPU, CSAF_READ );
          }
     }

     dfb_surface_allocation_ref( allocation );

     *ret_allocation = allocation;

out:
     dfb_surface_unlock( surface );

     return ret;
}

DFBResult
ISurface_Real::PreWriteBuffer(
                         CoreSurfaceBuffer                         *buffer,
                         const DFBRectangle                        *rect,
                         CoreSurfaceAllocation                    **ret_allocation
                         )
{
     DFBResult              ret;
     CoreSurfaceAllocation *allocation;
     CoreSurface           *surface    = obj;
     bool                   allocated  = false;

     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s()\n", __FUNCTION__ );

     D_MAGIC_ASSERT( buffer, CoreSurfaceBuffer );

     dfb_surface_lock( surface );

     if (surface->state & CSSF_DESTROYED) {
          dfb_surface_unlock( surface );
          return DFB_DESTROYED;
     }

     if (!buffer->surface) {
          dfb_surface_unlock( surface );
          return DFB_BUFFEREMPTY;
     }

     /* Use last read allocation if it's up to date... */
     if (buffer->read && direct_serial_check( &buffer->read->serial, &buffer->serial ))
          allocation = buffer->read;
     else {
          /* ...otherwise look for allocation with CPU access. */
          allocation = dfb_surface_buffer_find_allocation( buffer, CSAID_CPU, CSAF_WRITE, false );
          if (!allocation) {
               /* If no allocation exists, create one. */
               ret = dfb_surface_pools_allocate( buffer, CSAID_CPU, CSAF_WRITE, &allocation );
               if (ret) {
                    D_DERROR( ret, "Core/SurfBuffer: Buffer allocation failed!\n" );
                    goto out;
               }

               allocated = true;
          }
     }

     CORE_SURFACE_ALLOCATION_ASSERT( allocation );

     /* Synchronize with other allocations. */
     ret = dfb_surface_allocation_update( allocation, CSAF_WRITE );
     if (ret) {
          /* Destroy if newly created. */
          if (allocated)
               dfb_surface_allocation_decouple( allocation );
          goto out;
     }

     if (!(allocation->pool->desc.caps & CSPCAPS_WRITE)) {
          if (dfb_config->task_manager) {
               LockTask *task = new LockTask();

               task->AddAccess( allocation, CSAF_WRITE );

               task->Flush();

               if (task->Wait())
                    task->Done();
          }
          else {
               ret = dfb_surface_pool_prelock( allocation->pool, allocation, CSAID_CPU, CSAF_WRITE );
               if (ret) {
                    /* Destroy if newly created. */
                    if (allocated)
                         dfb_surface_allocation_decouple( allocation );
                    goto out;
               }

               manage_interlocks( allocation, CSAID_CPU, CSAF_WRITE );
          }
     }

     dfb_surface_allocation_ref( allocation );

     *ret_allocation = allocation;

out:
     dfb_surface_unlock( surface );

     return ret;
}

DFBResult
ISurface_Real::PreLockBuffer3(
                         CoreSurfaceBufferRole                      role,
                         u32                                        flip_count,
                         DFBSurfaceStereoEye                        eye,
                         CoreSurfaceAccessorID                      accessor,
                         CoreSurfaceAccessFlags                     access,
                         DFBBoolean                                 lock,
                         CoreSurfaceAllocation                    **ret_allocation
                         )
{
     DFBResult              ret;
     CoreSurfaceBuffer     *buffer;
     CoreSurfaceAllocation *allocation;
     CoreSurface           *surface    = obj;
     bool                   allocated  = false;

     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s( surface %p, role %d, count %u, eye %d, accessor 0x%02x, access 0x%02x, %slock )\n",
                 __FUNCTION__, surface, role, flip_count, eye, accessor, access, lock ? "" : "no " );

     ret = (DFBResult) dfb_surface_lock( surface );
     if (ret)
          return ret;

     if (surface->num_buffers < 1) {
          dfb_surface_unlock( surface );
          return DFB_BUFFEREMPTY;
     }

     buffer = dfb_surface_get_buffer3( surface, role, eye, flip_count );
     D_MAGIC_ASSERT( buffer, CoreSurfaceBuffer );

     D_DEBUG_AT( DirectFB_CoreSurface, "  -> buffer    %s\n", ToString_CoreSurfaceBuffer(buffer) );

     if (!lock && access & CSAF_READ) {
          if (fusion_vector_is_empty( &buffer->allocs )) {
               dfb_surface_unlock( surface );
               return DFB_NOALLOCATION;
          }
     }

     /* Look for allocation with proper access. */
     allocation = dfb_surface_buffer_find_allocation( buffer, accessor, access, lock );
     if (!allocation) {
          /* If no allocation exists, create one. */
          ret = dfb_surface_pools_allocate( buffer, accessor, access, &allocation );
          if (ret) {
               if (ret != DFB_NOVIDEOMEMORY && ret != DFB_UNSUPPORTED)
                    D_DERROR( ret, "Core/SurfBuffer: Buffer allocation failed!\n" );

               goto out;
          }

          allocated = true;
     }

     CORE_SURFACE_ALLOCATION_ASSERT( allocation );

     D_DEBUG_AT( DirectFB_CoreSurface, "  -> allocation %s\n", ToString_CoreSurfaceAllocation(allocation) );

     /* Synchronize with other allocations. */
     ret = dfb_surface_allocation_update( allocation, access );
     if (ret) {
          /* Destroy if newly created. */
          if (allocated)
               dfb_surface_allocation_decouple( allocation );
          goto out;
     }

     if (!lock) {
          if (access & CSAF_WRITE) {
               if (!(allocation->pool->desc.caps & CSPCAPS_WRITE))
                    lock = DFB_TRUE;
          }
          else if (access & CSAF_READ) {
               if (!(allocation->pool->desc.caps & CSPCAPS_READ))
                    lock = DFB_TRUE;
          }
     }

     if (lock) {
          if (dfb_config->task_manager) {
               D_ASSERT( accessor == CSAID_CPU );

               LockTask *task = new LockTask();

               task->AddAccess( allocation, access );

               task->Flush();

               if (task->Wait())   // FIXME: make async, signal from Push()
                    task->Done();
          }
          else {
               ret = dfb_surface_pool_prelock( allocation->pool, allocation, accessor, access );
               if (ret) {
                    /* Destroy if newly created. */
                    if (allocated)
                         dfb_surface_allocation_decouple( allocation );
                    goto out;
               }

               manage_interlocks( allocation, accessor, access );
          }
     }

     dfb_surface_allocation_ref( allocation );

     *ret_allocation = allocation;

out:
     dfb_surface_unlock( surface );

     return ret;
}


DFBResult
ISurface_Real::CreateClient(
                         CoreSurfaceClient                           **ret_client
                         )
{
     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s(%p)\n", __FUNCTION__, obj );

     D_ASSERT( ret_client != NULL );

     return dfb_surface_client_create( core, obj, ret_client );
}


DFBResult
ISurface_Real::Flip2(
                   DFBBoolean                                    swap,
                   const DFBRegion                              *left,
                   const DFBRegion                              *right,
                   DFBSurfaceFlipFlags                           flags,
                   s64                                           timestamp
                   )
{
     DFBResult ret = DFB_OK;
     DFBRegion l, r;

     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s( timestamp %lld )\n", __FUNCTION__, (long long) timestamp );

     dfb_surface_lock( obj );

     if (left)
          l = *left;
     else {
          l.x1 = 0;
          l.y1 = 0;
          l.x2 = obj->config.size.w - 1;
          l.y2 = obj->config.size.h - 1;
     }

     if (right)
          r = *right;
     else
          r = l;

     if (obj->config.caps & DSCAPS_FLIPPING) {
         if (obj->config.caps & DSCAPS_STEREO) {
              if ((flags & DSFLIP_SWAP) ||
                  (!(flags & DSFLIP_BLIT) &&
                   l.x1 == 0 && l.y1 == 0 &&
                   l.x2 == obj->config.size.w - 1 &&
                   l.y2 == obj->config.size.h - 1 &&
                   r.x1 == 0 && r.y1 == 0 &&
                   r.x2 == obj->config.size.w - 1 &&
                   r.y2 == obj->config.size.h - 1))
              {
                  ret = dfb_surface_flip_buffers( obj, swap );
                  if (ret)
                      goto out;
              }
              else {
                  if (left)
                      dfb_gfx_copy_regions_client( obj, CSBR_BACK, DSSE_LEFT,
                                                   obj, CSBR_FRONT, DSSE_LEFT,
                                                   &l, 1, 0, 0, NULL );
                  if (right)
                      dfb_gfx_copy_regions_client( obj, CSBR_BACK, DSSE_RIGHT,
                                                   obj, CSBR_FRONT, DSSE_RIGHT,
                                                   &r, 1, 0, 0, NULL );
              }
         }
         else {
             if ((flags & DSFLIP_SWAP) ||
                 (!(flags & DSFLIP_BLIT) &&
                  l.x1 == 0 && l.y1 == 0 &&
                  l.x2 == obj->config.size.w - 1 &&
                  l.y2 == obj->config.size.h - 1))
             {
                  ret = dfb_surface_flip_buffers( obj, swap );
                  if (ret)
                      goto out;
             }
             else {
                  dfb_gfx_copy_regions_client( obj, CSBR_BACK, DSSE_LEFT,
                                               obj, CSBR_FRONT, DSSE_LEFT,
                                               &l, 1, 0, 0, NULL );
             }
         }
     }

     // FIXME: this always updates full when a side is empty
     dfb_surface_dispatch_update( obj, &l, &r, timestamp, flags );

out:
     dfb_surface_unlock( obj );

     return ret;
}


DFBResult
ISurface_Real::Allocate( CoreSurfaceBufferRole   role,
                         DFBSurfaceStereoEye     eye,
                         const char             *key,
                         u32                     key_len,
                         u64                     handle,
                         CoreSurfaceAllocation **ret_allocation )
{
     DFBResult              ret;
     CoreSurfaceBuffer     *buffer;
     CoreSurfaceAllocation *allocation;

     D_ASSERT( key != NULL );
     D_ASSERT( key_len > 0 );
     D_ASSERT( key[key_len-1] == 0 );
     D_ASSERT( ret_allocation != NULL );

     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s( role %d, eye %d, key '%s', handle 0x%08llx )\n",
                 __FUNCTION__, role, eye, key, (unsigned long long) handle );

     ret = (DFBResult) dfb_surface_lock( obj );
     if (ret)
          return ret;

     if (obj->num_buffers == 0) {
          ret = DFB_NOBUFFER;
          goto out;
     }

     buffer = dfb_surface_get_buffer3( obj, role, eye, obj->flips );
     CORE_SURFACE_BUFFER_ASSERT( buffer );

     ret = dfb_surface_pools_allocate_key( buffer, key, handle, &allocation );
     if (ret)
          goto out;

     CORE_SURFACE_ALLOCATION_ASSERT( allocation );

     dfb_surface_allocation_update( allocation, CSAF_WRITE );

     ret = (DFBResult) dfb_surface_allocation_ref( allocation );
     if (ret)
          goto out;

     *ret_allocation = allocation;

out:
     dfb_surface_unlock( obj );

     return ret;
}

DFBResult
ISurface_Real::GetAllocation( CoreSurfaceBufferRole   role,
                              DFBSurfaceStereoEye     eye,
                              const char             *key,
                              u32                     key_len,
                              CoreSurfaceAllocation **ret_allocation )
{
     DFBResult              ret;
     CoreSurfaceBuffer     *buffer;
     CoreSurfaceAllocation *allocation;

     D_ASSERT( key != NULL );
     D_ASSERT( key_len > 0 );
     D_ASSERT( key[key_len-1] == 0 );
     D_ASSERT( ret_allocation != NULL );

     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s( role %d, eye %d, key '%s' )\n", __FUNCTION__, role, eye, key );

     if (eye != DSSE_LEFT && eye != DSSE_RIGHT)
          return DFB_INVARG;

     ret = (DFBResult) dfb_surface_lock( obj );
     if (ret)
          return ret;

     if (obj->num_buffers == 0) {
          ret = DFB_NOBUFFER;
          goto out;
     }

     if (role > obj->num_buffers - 1) {
          ret = DFB_LIMITEXCEEDED;
          goto out;
     }

     if (eye == DSSE_RIGHT && !(obj->config.caps & DSCAPS_STEREO)) {
          ret = DFB_INVAREA;
          goto out;
     }

     buffer = dfb_surface_get_buffer3( obj, role, eye, obj->flips );  // FIXME: with or without flips? or let app decide?
     CORE_SURFACE_BUFFER_ASSERT( buffer );

     allocation = dfb_surface_buffer_find_allocation_key( buffer, key );
     if (!allocation) {
          if (!0) {
               ret = DFB_ITEMNOTFOUND;
               goto out;
          }

          ret = dfb_surface_pools_allocate_key( buffer, key, 0, &allocation );
          if (ret)
               goto out;
     }

     CORE_SURFACE_ALLOCATION_ASSERT( allocation );

     dfb_surface_allocation_update( allocation, CSAF_WRITE );

     ret = (DFBResult) dfb_surface_allocation_ref( allocation );
     if (ret)
          goto out;

     *ret_allocation = allocation;

out:
     dfb_surface_unlock( obj );

     return ret;
}


DFBResult
ISurface_Real::DispatchUpdate(
                   DFBBoolean                                    swap,
                   const DFBRegion                              *left,
                   const DFBRegion                              *right,
                   DFBSurfaceFlipFlags                           flags,
                   s64                                           timestamp,
                   u32                                           flip_count
                   )
{
     DFBResult ret = DFB_OK;
     DFBRegion l, r;

     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s( timestamp %lld, flip_count %u )\n",
                 __FUNCTION__, (long long) timestamp, flip_count );

     dfb_surface_lock( obj );

     if (left)
          l = *left;
     else {
          l.x1 = 0;
          l.y1 = 0;
          l.x2 = obj->config.size.w - 1;
          l.y2 = obj->config.size.h - 1;
     }

     if (right)
          r = *right;
     else
          r = l;

     if (!(flags & DSFLIP_UPDATE))
          obj->flips = flip_count;

     // FIXME: this always updates full when a side is empty
     dfb_surface_dispatch_update( obj, &l, &r, timestamp, flags );

     dfb_surface_unlock( obj );

     return ret;
}

DFBResult
ISurface_Real::GetBuffers( DFBSurfaceBufferID *ret_buffer_ids,
                           u32                 ids_max,
                           u32                *ret_ids_len )
{
     DFBResult ret;
     u32       count  = 0;

     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s(  )\n",
                 __FUNCTION__ );

     if (ids_max < 1)
          return DFB_INVARG;

     ret = (DFBResult) dfb_surface_lock( obj );
     if (ret)
          return ret;

     if (obj->num_buffers == 0) {
          ret = DFB_NOBUFFER;
          goto out;
     }

     for (int i=0; i<obj->num_buffers; i++) {
          CoreSurfaceBuffer *buffer;

          if (count == ids_max) {
               ret = DFB_LIMITEXCEEDED;
               goto out;
          }

          buffer = obj->left_buffers[ obj->buffer_indices[i] ];
          CORE_SURFACE_BUFFER_ASSERT( buffer );

          ret_buffer_ids[count++] = buffer->object.id;


          if (obj->config.caps & DSCAPS_STEREO) {
               if (count == ids_max) {
                    ret = DFB_LIMITEXCEEDED;
                    goto out;
               }

               buffer = obj->right_buffers[ obj->buffer_indices[i] ];
               CORE_SURFACE_BUFFER_ASSERT( buffer );

               ret_buffer_ids[count++] = buffer->object.id;
          }
     }

     *ret_ids_len = count;

out:
     dfb_surface_unlock( obj );

     return ret;
}

DFBResult
ISurface_Real::GetOrAllocate( DFBSurfaceBufferID        buffer_id,
                              const char               *key,
                              u32                       key_len,
                              u64                       handle,
                              DFBSurfaceAllocationOps   ops,
                              CoreSurfaceAllocation   **ret_allocation )
{
     DFBResult              ret;
     CoreSurfaceBuffer     *buffer     = NULL;
     CoreSurfaceAllocation *allocation = NULL;

     D_ASSERT( key != NULL );
     D_ASSERT( key_len > 0 );
     D_ASSERT( key[key_len-1] == 0 );
     D_ASSERT( ret_allocation != NULL );

     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s( buffer id 0x%x, key '%s', handle 0x%llx, ops 0x%04x )\n",
                 __FUNCTION__, buffer_id, key, (long long)handle, ops );

     ret = (DFBResult) dfb_surface_lock( obj );
     if (ret)
          return ret;

     ret = dfb_core_get_surface_buffer( core, buffer_id, &buffer );
     if (ret)
          goto out;

     if (!buffer->surface) {
          ret = DFB_DEAD;
          goto out;
     }

     if (buffer->surface != obj) {
          ret = DFB_NOSUCHINSTANCE;
          goto out;
     }

     CORE_SURFACE_BUFFER_ASSERT( buffer );

     D_DEBUG_AT( DirectFB_CoreSurface, "  -> buffer   %s\n", *ToString<CoreSurfaceBuffer>( *buffer ) );

     allocation = dfb_surface_buffer_find_allocation_key( buffer, key );

     if (allocation) {
          CORE_SURFACE_ALLOCATION_ASSERT( allocation );

          D_DEBUG_AT( DirectFB_CoreSurface, "  -> existing %s\n", *ToString<CoreSurfaceAllocation>( *allocation ) );

          if (!(ops & DSAO_KEEP)) {
               dfb_surface_allocation_decouple( allocation );
               allocation = NULL;
          }
          else if (ops & DSAO_NEW)
               allocation = NULL;
          else if (ops & DSAO_HANDLE) {
               ret = dfb_surface_pool_update_key( allocation->pool, buffer, key, handle, allocation );
               if (ret) {
                    allocation = NULL;
                    goto out;
               }
          }
     }
     else if (ops & DSAO_EXISTING) {
          ret = DFB_ITEMNOTFOUND;
          goto out;
     }

     if (!allocation) {
          ret = dfb_surface_pools_allocate_key( buffer, key, handle, &allocation );
          if (ret)
               goto out;

          D_DEBUG_AT( DirectFB_CoreSurface, "  -> new      %s\n", *ToString<CoreSurfaceAllocation>( *allocation ) );
     }

     ret = (DFBResult) dfb_surface_allocation_ref( allocation );
     if (ret) {
          allocation = NULL;
          goto out;
     }

     CORE_SURFACE_ALLOCATION_ASSERT( allocation );

     if (ops & DSAO_UPDATE)
          dfb_surface_allocation_update( allocation, CSAF_READ );

     if (ops & DSAO_UPDATED) {
          direct_serial_update( &allocation->serial, &buffer->serial );
          dfb_surface_allocation_update( allocation, CSAF_WRITE );
     }

     *ret_allocation = allocation;

out:
     if (allocation && ret)
          dfb_surface_allocation_unref( allocation );

     if (buffer)
          dfb_surface_buffer_unref( buffer );

     dfb_surface_unlock( obj );

     return ret;
}


DFBResult
ISurface_Real::DispatchUpdate2(
                   DFBSurfaceFlipFlags                        flags,
                   s64                                        timestamp,
                   u32                                        left_id,
                   u32                                        left_serial,
                   const DFBRegion                           *left_region,
                   u32                                        right_id,
                   u32                                        right_serial,
                   const DFBRegion                           *right_region
                   )
{
     DFBResult ret = DFB_OK;
     DFBRegion l, r;

     D_DEBUG_AT( DirectFB_CoreSurface, "ISurface_Real::%s( flags 0x%08x, timestamp %lld, left id 0x%x serial 0x%x )\n",
                 __FUNCTION__, flags, (long long) timestamp, left_id, left_serial );

     dfb_surface_lock( obj );

     if (left_region)
          l = *left_region;
     else {
          l.x1 = 0;
          l.y1 = 0;
          l.x2 = obj->config.size.w - 1;
          l.y2 = obj->config.size.h - 1;
     }

     if (right_region)
          r = *right_region;
     else
          r = l;

//     if (!(flags & DSFLIP_UPDATE))
//          obj->flips = flip_count;

     // FIXME: this always updates full when a side is empty
     dfb_surface_dispatch_update( obj, &l, &r, timestamp, flags );

     dfb_surface_unlock( obj );

     return ret;
}


}

