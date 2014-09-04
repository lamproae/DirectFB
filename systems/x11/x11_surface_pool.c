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

#include <direct/debug.h>
#include <direct/hash.h>
#include <direct/mem.h>

#include <fusion/shmalloc.h>

#include <core/surface_pool.h>

#include "x11.h"
#include "x11image.h"
#include "x11_surface_pool.h"

D_DEBUG_DOMAIN( X11_Surfaces, "X11/Surfaces", "X11 System Surface Pool" );

/**********************************************************************************************************************/

typedef struct {
} x11PoolData;

typedef struct {
     pthread_mutex_t  lock;
     DirectHash      *hash;

     DFBX11          *x11;
} x11PoolLocalData;

/**********************************************************************************************************************/

static int
x11PoolDataSize( void )
{
     return sizeof(x11PoolData);
}

static int
x11PoolLocalDataSize( void )
{
     return sizeof(x11PoolLocalData);
}

static int
x11AllocationDataSize( void )
{
     return sizeof(x11AllocationData);
}

static DFBResult
x11InitPool( CoreDFB                    *core,
             CoreSurfacePool            *pool,
             void                       *pool_data,
             void                       *pool_local,
             void                       *system_data,
             CoreSurfacePoolDescription *ret_desc )
{
     DFBResult         ret;
     x11PoolLocalData *local = pool_local;
     DFBX11           *x11   = system_data;

     D_DEBUG_AT( X11_Surfaces, "%s()\n", __FUNCTION__ );

     D_MAGIC_ASSERT( pool, CoreSurfacePool );
     D_ASSERT( ret_desc != NULL );

     local->x11 = x11;

     ret_desc->caps              = CSPCAPS_VIRTUAL;
     ret_desc->access[CSAID_CPU] = CSAF_READ | CSAF_WRITE | CSAF_SHARED;
     ret_desc->types             = CSTF_LAYER | CSTF_WINDOW | CSTF_CURSOR | CSTF_FONT | CSTF_SHARED | CSTF_EXTERNAL | CSTF_INTERNAL;
     ret_desc->priority          = CSPP_PREFERED;

     /* For showing our X11 window */
     ret_desc->access[CSAID_LAYER0]  = CSAF_READ;
     ret_desc->access[CSAID_LAYER1]  = CSAF_READ;
     ret_desc->access[CSAID_LAYER2]  = CSAF_READ;
     ret_desc->access[CSAID_LAYER3]  = CSAF_READ;
     ret_desc->access[CSAID_LAYER4]  = CSAF_READ;
     ret_desc->access[CSAID_LAYER5]  = CSAF_READ;
     ret_desc->access[CSAID_LAYER6]  = CSAF_READ;
     ret_desc->access[CSAID_LAYER7]  = CSAF_READ;
     ret_desc->access[CSAID_LAYER8]  = CSAF_READ;
     ret_desc->access[CSAID_LAYER9]  = CSAF_READ;
     ret_desc->access[CSAID_LAYER10] = CSAF_READ;
     ret_desc->access[CSAID_LAYER11] = CSAF_READ;
     ret_desc->access[CSAID_LAYER12] = CSAF_READ;
     ret_desc->access[CSAID_LAYER13] = CSAF_READ;
     ret_desc->access[CSAID_LAYER14] = CSAF_READ;
     ret_desc->access[CSAID_LAYER15] = CSAF_READ;

     snprintf( ret_desc->name, DFB_SURFACE_POOL_DESC_NAME_LENGTH, "X11 Shm Images" );

     ret = direct_hash_create( 7, &local->hash );
     if (ret) {
          D_DERROR( ret, "X11/Surfaces: Could not create local hash table!\n" );
          return ret;
     }

     pthread_mutex_init( &local->lock, NULL );

     return DFB_OK;
}

static DFBResult
x11JoinPool( CoreDFB                    *core,
             CoreSurfacePool            *pool,
             void                       *pool_data,
             void                       *pool_local,
             void                       *system_data )
{
     DFBResult         ret;
     x11PoolLocalData *local = pool_local;
     DFBX11           *x11   = system_data;

     D_DEBUG_AT( X11_Surfaces, "%s()\n", __FUNCTION__ );

     D_MAGIC_ASSERT( pool, CoreSurfacePool );

     local->x11 = x11;

     ret = direct_hash_create( 7, &local->hash );
     if (ret) {
          D_DERROR( ret, "X11/Surfaces: Could not create local hash table!\n" );
          return ret;
     }

     pthread_mutex_init( &local->lock, NULL );

     return DFB_OK;
}

static DFBResult
x11DestroyPool( CoreSurfacePool *pool,
                void            *pool_data,
                void            *pool_local )
{
     x11PoolLocalData *local = pool_local;

     D_DEBUG_AT( X11_Surfaces, "%s()\n", __FUNCTION__ );

     D_MAGIC_ASSERT( pool, CoreSurfacePool );

     pthread_mutex_destroy( &local->lock );

     direct_hash_destroy( local->hash );

     return DFB_OK;
}

static DFBResult
x11LeavePool( CoreSurfacePool *pool,
              void            *pool_data,
              void            *pool_local )
{
     x11PoolLocalData *local = pool_local;

     D_DEBUG_AT( X11_Surfaces, "%s()\n", __FUNCTION__ );

     D_MAGIC_ASSERT( pool, CoreSurfacePool );

     pthread_mutex_destroy( &local->lock );

     direct_hash_destroy( local->hash );

     return DFB_OK;
}

static DFBResult
x11TestConfig( CoreSurfacePool         *pool,
               void                    *pool_data,
               void                    *pool_local,
               CoreSurfaceBuffer       *buffer,
               const CoreSurfaceConfig *config )
{
     x11PoolLocalData *local  = pool_local;
     DFBX11           *x11    = local->x11;
     //DFBX11Shared     *shared = x11->shared;

     /* Provide a fallback only if no virtual physical pool is allocated... */
     //if (!shared->vpsmem_length)
     //     return DFB_OK;

     /* Pass NULL image for probing */
     return x11ImageInit( x11, NULL, config->size.w, config->size.h, config->format );
}

static DFBResult
x11PreAlloc( CoreSurfacePool             *pool,
             void                        *pool_data,
             void                        *pool_local,
             const DFBSurfaceDescription *description,
             CoreSurfaceConfig           *config )
{
     unsigned int i, num = 1;

     D_DEBUG_AT( X11_Surfaces, "%s()\n", __FUNCTION__ );

     D_MAGIC_ASSERT( pool, CoreSurfacePool );

     if (!(config->caps & DSCAPS_VIDEOONLY))
          return DFB_UNSUPPORTED;

     if (config->caps & DSCAPS_DOUBLE)
          num = 2;
     else if (config->caps & DSCAPS_TRIPLE)
          num = 3;

     for (i=0; i<num; i++) {
          if (!config->preallocated[i].handle)
               return DFB_UNSUPPORTED;
     }

     return DFB_OK;
}

static DFBResult
x11CheckKey( CoreSurfacePool   *pool,
             void              *pool_data,
             void              *pool_local,
             CoreSurfaceBuffer *buffer,
             const char        *key,
             u64                handle )
{
     D_DEBUG_AT( X11_Surfaces, "%s()\n", __FUNCTION__ );

     D_MAGIC_ASSERT( pool, CoreSurfacePool );
     D_MAGIC_ASSERT( buffer, CoreSurfaceBuffer );

     return DFB_UNSUPPORTED;

     if (!strcmp( key, "Pixmap/X11" ))
          return DFB_OK;

     if (!strcmp( key, "Window/X11" ))
          return DFB_OK;

     return DFB_UNSUPPORTED;
}

static DFBResult
x11AllocateBuffer( CoreSurfacePool       *pool,
                   void                  *pool_data,
                   void                  *pool_local,
                   CoreSurfaceBuffer     *buffer,
                   CoreSurfaceAllocation *allocation,
                   void                  *alloc_data )
{
     CoreSurface       *surface;
     x11AllocationData *alloc = alloc_data;
     x11PoolLocalData  *local = pool_local;
     DFBX11            *x11   = local->x11;

     D_DEBUG_AT( X11_Surfaces, "%s()\n", __FUNCTION__ );

     D_MAGIC_ASSERT( pool, CoreSurfacePool );
     D_MAGIC_ASSERT( buffer, CoreSurfaceBuffer );

     surface = buffer->surface;
     D_MAGIC_ASSERT( surface, CoreSurface );

     dfb_surface_calc_buffer_size( surface, 8, 8, &alloc->pitch, &allocation->size );

     if (buffer->type & CSTF_PREALLOCATED) {
          D_DEBUG_AT( X11_Surfaces, "  -> preallocated from %p\n", buffer->config.preallocated[0].handle );

          alloc->type = X11_ALLOC_PIXMAP;
          alloc->xid  = (unsigned long) buffer->config.preallocated[0].handle;
     }
     else {
          if (x11ImageInit( x11, &alloc->image, surface->config.size.w, surface->config.size.h, surface->config.format ) == DFB_OK) {
               alloc->type  = X11_ALLOC_IMAGE;
               alloc->pitch = alloc->image.pitch;

               allocation->size = surface->config.size.h * alloc->image.pitch;
          }
          else
               alloc->type = X11_ALLOC_SHM;
     }

     return DFB_OK;
}

static DFBResult
x11AllocateKey( CoreSurfacePool       *pool,
                void                  *pool_data,
                void                  *pool_local,
                CoreSurfaceBuffer     *buffer,
                const char            *key,
                u64                    handle,
                CoreSurfaceAllocation *allocation,
                void                  *alloc_data )
{
     CoreSurface       *surface;
     x11AllocationData *alloc = alloc_data;

     D_DEBUG_AT( X11_Surfaces, "%s( %s, 0x%08llx )\n", __FUNCTION__, key, (unsigned long long) handle );

     D_MAGIC_ASSERT( pool, CoreSurfacePool );
     D_MAGIC_ASSERT( buffer, CoreSurfaceBuffer );

     surface = buffer->surface;
     D_MAGIC_ASSERT( surface, CoreSurface );

     if (!strcmp( key, "Pixmap/X11" )) {
          D_DEBUG_AT( X11_Surfaces, "  -> Pixmap/X11\n" );

          alloc->type = X11_ALLOC_PIXMAP;
     }
     else if (!strcmp( key, "Window/X11" )) {
          D_DEBUG_AT( X11_Surfaces, "  -> Window/X11\n" );

          alloc->type = X11_ALLOC_WINDOW;
     }
     else {
          D_BUG( "unexpected key '%s'", key );
          return DFB_BUG;
     }

     dfb_surface_calc_buffer_size( surface, 8, 8, &alloc->pitch, &allocation->size );

     alloc->xid = (unsigned long) handle;

     return DFB_OK;
}

static DFBResult
x11DeallocateBuffer( CoreSurfacePool       *pool,
                     void                  *pool_data,
                     void                  *pool_local,
                     CoreSurfaceBuffer     *buffer,
                     CoreSurfaceAllocation *allocation,
                     void                  *alloc_data )
{
     x11AllocationData *alloc  = alloc_data;
     x11PoolLocalData  *local  = pool_local;
     DFBX11            *x11    = local->x11;
     DFBX11Shared      *shared = x11->shared;
     void              *addr;

     D_DEBUG_AT( X11_Surfaces, "%s()\n", __FUNCTION__ );

     D_MAGIC_ASSERT( pool, CoreSurfacePool );

     CORE_SURFACE_ALLOCATION_ASSERT( allocation );

     switch (alloc->type) {
          case X11_ALLOC_PIXMAP:
          case X11_ALLOC_WINDOW:
               if (allocation->type & CSTF_PREALLOCATED) {
                    // don't delete
               }
               else
                    XFreePixmap( x11->display, alloc->xid );
               break;

          case X11_ALLOC_IMAGE:
               x11ImageDestroy( x11, &alloc->image );

               // FIXME: also detach in other processes! (e.g. via reactor)
               addr = direct_hash_lookup( local->hash, alloc->image.seginfo.shmid );
               if (addr) {
                    x11ImageDetach( &alloc->image, addr );

                    direct_hash_remove( local->hash, alloc->image.seginfo.shmid );
               }
               break;

          case X11_ALLOC_SHM:
               if (alloc->ptr)
                    SHFREE( shared->data_shmpool, alloc->ptr );
               break;

          default:
               D_BUG( "unexpected allocation type %d\n", alloc->type );
               return DFB_BUG;
     }

     return DFB_OK;
}

static DFBResult
x11Lock( CoreSurfacePool       *pool,
         void                  *pool_data,
         void                  *pool_local,
         CoreSurfaceAllocation *allocation,
         void                  *alloc_data,
         CoreSurfaceBufferLock *lock )
{
     DFBResult          ret;
     x11PoolLocalData  *local  = pool_local;
     x11AllocationData *alloc  = alloc_data;
     DFBX11            *x11    = local->x11;
     DFBX11Shared      *shared = x11->shared;
     void              *addr;

     D_DEBUG_AT( X11_Surfaces, "%s( %p )\n", __FUNCTION__, allocation );

     D_MAGIC_ASSERT( pool, CoreSurfacePool );
     D_MAGIC_ASSERT( allocation, CoreSurfaceAllocation );
     D_MAGIC_ASSERT( lock, CoreSurfaceBufferLock );

     D_ASSERT( local->hash != NULL );

     pthread_mutex_lock( &local->lock );

     switch (alloc->type) {
          case X11_ALLOC_PIXMAP:
          case X11_ALLOC_WINDOW:
               lock->handle = (void*)(long) alloc->xid;
               break;

          case X11_ALLOC_IMAGE:
               addr = direct_hash_lookup( local->hash, alloc->image.seginfo.shmid );

               if (!addr) {
                    ret = x11ImageAttach( &alloc->image, &addr );
                    if (ret) {
                         D_DERROR( ret, "X11/Surfaces: x11ImageAttach() failed!\n" );
                         pthread_mutex_unlock( &local->lock );
                         return ret;
                    }

                    direct_hash_insert( local->hash, alloc->image.seginfo.shmid, addr );

                    /* FIXME: When to remove/detach? */
               }

               lock->addr   = addr;
               lock->handle = &alloc->image;
               break;

          case X11_ALLOC_SHM:
               if (!alloc->ptr) {
                    D_DEBUG_AT( X11_Surfaces, "  -> allocating memory in data_shmpool (%d bytes)\n", allocation->size );

                    alloc->ptr = SHCALLOC( shared->data_shmpool, 1, allocation->size );
                    if (!alloc->ptr) {
                         pthread_mutex_unlock( &local->lock );
                         return D_OOSHM();
                    }
               }

               lock->addr = alloc->ptr;
               break;

          default:
               D_BUG( "unexpected allocation type %d\n", alloc->type );
               pthread_mutex_unlock( &local->lock );
               return DFB_BUG;
     }

     lock->pitch = alloc->pitch;

     pthread_mutex_unlock( &local->lock );

     return DFB_OK;
}

static DFBResult
x11Unlock( CoreSurfacePool       *pool,
           void                  *pool_data,
           void                  *pool_local,
           CoreSurfaceAllocation *allocation,
           void                  *alloc_data,
           CoreSurfaceBufferLock *lock )
{
     D_DEBUG_AT( X11_Surfaces, "%s( %p )\n", __FUNCTION__, allocation );

     D_MAGIC_ASSERT( pool, CoreSurfacePool );
     D_MAGIC_ASSERT( allocation, CoreSurfaceAllocation );
     D_MAGIC_ASSERT( lock, CoreSurfaceBufferLock );

     /* FIXME: Check overhead of attach/detach per lock/unlock. */

     return DFB_OK;
}

const SurfacePoolFuncs x11SurfacePoolFuncs = {
     .PoolDataSize       = x11PoolDataSize,
     .PoolLocalDataSize  = x11PoolLocalDataSize,
     .AllocationDataSize = x11AllocationDataSize,

     .InitPool           = x11InitPool,
     .JoinPool           = x11JoinPool,
     .DestroyPool        = x11DestroyPool,
     .LeavePool          = x11LeavePool,

     .TestConfig         = x11TestConfig,

     .PreAlloc           = x11PreAlloc,
     .CheckKey           = x11CheckKey,
     .AllocateBuffer     = x11AllocateBuffer,
     .AllocateKey        = x11AllocateKey,
     .DeallocateBuffer   = x11DeallocateBuffer,

     .Lock               = x11Lock,
     .Unlock             = x11Unlock,
};

