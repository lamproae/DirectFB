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
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>

#include <sys/param.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <direct/clock.h>
#include <direct/debug.h>
#include <direct/direct.h>
#include <direct/map.h>
#include <direct/mem.h>
#include <direct/memcpy.h>
#include <direct/messages.h>
#include <direct/signals.h>
#include <direct/thread.h>
#include <direct/trace.h>
#include <direct/util.h>

#include <fusion/conf.h>
#include <fusion/hash.h>

#include "fusion_internal.h"

#include <fusion/shmalloc.h>

#include <fusion/shm/shm.h>

D_DEBUG_DOMAIN( Fusion_Main,          "Fusion/Main",          "Fusion - High level IPC" );
D_DEBUG_DOMAIN( Fusion_Main_Dispatch, "Fusion/Main/Dispatch", "Fusion - High level IPC Dispatch" );

#if FUSION_BUILD_MULTI

#include <sys/mman.h>
#include <sys/utsname.h>

/**********************************************************************************************************************/

static void                      *fusion_dispatch_loop ( DirectThread *thread,
                                                         void         *arg );
static void                      *fusion_deferred_loop ( DirectThread *thread,
                                                         void         *arg );

/**********************************************************************************************************************/

static void                       fusion_fork_handler_prepare( void );
static void                       fusion_fork_handler_parent( void );
static void                       fusion_fork_handler_child( void );

/**********************************************************************************************************************/

static FusionWorld     *fusion_worlds[FUSION_MAX_WORLDS];
static pthread_mutex_t  fusion_worlds_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t   fusion_init_once   = PTHREAD_ONCE_INIT;

/**********************************************************************************************************************/

int
fusion_fd( const FusionWorld *world )
{
     int                index;
     FusionWorldShared *shared;

     D_MAGIC_ASSERT( world, FusionWorld );

     shared = world->shared;
     D_MAGIC_ASSERT( shared, FusionWorldShared );

     index = shared->world_index;

     D_ASSERT( index >= 0 );
     D_ASSERT( index < FUSION_MAX_WORLDS );

     D_ASSERT( world == fusion_worlds[index] );

     D_MAGIC_ASSERT( world, FusionWorld );

     return world->fusion_fd;
}

int
_fusion_fd( const FusionWorldShared *shared )
{
     int          index;
     FusionWorld *world;

     D_MAGIC_ASSERT( shared, FusionWorldShared );

     index = shared->world_index;

     D_ASSERT( index >= 0 );
     D_ASSERT( index < FUSION_MAX_WORLDS );

     world = fusion_worlds[index];

     D_MAGIC_ASSERT( world, FusionWorld );

     return world->fusion_fd;
}

FusionID
_fusion_id( const FusionWorldShared *shared )
{
     int          index;
     FusionWorld *world;

     D_MAGIC_ASSERT( shared, FusionWorldShared );

     index = shared->world_index;

     D_ASSERT( index >= 0 );
     D_ASSERT( index < FUSION_MAX_WORLDS );

     world = fusion_worlds[index];

     D_MAGIC_ASSERT( world, FusionWorld );

     return world->fusion_id;
}

FusionWorld *
_fusion_world( const FusionWorldShared *shared )
{
     int          index;
     FusionWorld *world;

     D_MAGIC_ASSERT( shared, FusionWorldShared );

     index = shared->world_index;

     D_ASSERT( index >= 0 );
     D_ASSERT( index < FUSION_MAX_WORLDS );

     world = fusion_worlds[index];

     D_MAGIC_ASSERT( world, FusionWorld );

     return world;
}

/**********************************************************************************************************************/

static void
init_once( void )
{
     struct utsname uts;
     int            i, j, k, l;

     if (fusion_config->fork_handler)
          pthread_atfork( fusion_fork_handler_prepare, fusion_fork_handler_parent, fusion_fork_handler_child );

     if (uname( &uts ) < 0) {
          D_PERROR( "Fusion/Init: uname() failed!\n" );
          return;
     }
     
#if !FUSION_BUILD_KERNEL
     D_INFO( "Fusion/Init: "
             "Builtin Implementation is still experimental! Crash/Deadlocks might occur!\n" );
#endif

     if (fusion_config->madv_remove_force) {
          if (fusion_config->madv_remove)
               D_INFO( "Fusion/SHM: Using MADV_REMOVE (forced)\n" );
          else
               D_INFO( "Fusion/SHM: Not using MADV_REMOVE (forced)!\n" );
     }
     else {
          switch (sscanf( uts.release, "%d.%d.%d.%d", &i, &j, &k, &l )) {
               case 3:
                    l = 0;
               case 4:
                    if (((i << 24) | (j << 16) | (k << 8) | l) >= 0x02061302)
                         fusion_config->madv_remove = true;
                    break;

               default:
                    D_WARN( "could not parse kernel version '%s'", uts.release );
          }

          if (fusion_config->madv_remove)
               D_INFO( "Fusion/SHM: Using MADV_REMOVE (%d.%d.%d.%d >= 2.6.19.2)\n", i, j, k, l );
          else
               D_INFO( "Fusion/SHM: NOT using MADV_REMOVE (%d.%d.%d.%d < 2.6.19.2)! [0x%08x]\n",
                       i, j, k, l, (i << 24) | (j << 16) | (k << 8) | l );
     }
}

/**********************************************************************************************************************/

static bool
refs_map_compare( DirectMap    *map,
                  const void   *_key,
                  void         *object,
                  void         *ctx )
{
     const FusionRefSlaveKey *key   = _key;
     FusionRefSlaveEntry     *entry = object;

     return key->fusion_id == entry->key.fusion_id && key->ref_id == entry->key.ref_id;
}

static unsigned int
refs_map_hash( DirectMap    *map,
               const void   *_key,
               void         *ctx )
{
     const FusionRefSlaveKey *key = _key;

     return key->ref_id * 131 + key->fusion_id;
}


static bool
refs_map_slave_compare( DirectMap    *map,
                        const void   *_key,
                        void         *object,
                        void         *ctx )
{
     const int *key   = _key;
     FusionRef *entry = object;

     return *key == entry->multi.id;
}

static unsigned int
refs_map_slave_hash( DirectMap    *map,
                     const void   *_key,
                     void         *ctx )
{
     const int *key = _key;

     return *key;
}


static FusionCallHandlerResult
world_refs_call( int           caller,   /* fusion id of the caller */
                 int           call_arg, /* optional call parameter */
                 void         *call_ptr, /* optional call parameter */
                 void         *ctx,      /* optional handler context */
                 unsigned int  serial,
                 int          *ret_val )
{
     FusionWorld         *world = ctx;
     FusionRefSlaveKey    key;
     FusionRefSlaveEntry *slave;

     key.fusion_id = caller;
     key.ref_id    = call_arg;

     direct_mutex_lock( &world->refs_lock );
     slave = direct_map_lookup( world->refs_map, &key );
     direct_mutex_unlock( &world->refs_lock );

     if (!slave) {
          D_WARN( "slave (%d) ref (%d) not found", caller, call_arg );
          return FCHR_RETURN;
     }

     fusion_ref_down( slave->ref, false );

     direct_mutex_lock( &world->refs_lock );

     if (!--slave->refs) {
          direct_map_remove( world->refs_map, &key );

          D_FREE( slave );
     }

     direct_mutex_unlock( &world->refs_lock );

     return FCHR_RETURN;
}

/**********************************************************************************************************************/
/**********************************************************************************************************************/

#if FUSION_BUILD_KERNEL

static void
fusion_world_fork( FusionWorld *world )
{
     int                fd;
     char               buf1[20];
     char               buf2[20];
     FusionEnter        enter;
     FusionFork         fork;
     FusionWorldShared *shared;

     D_MAGIC_ASSERT( world, FusionWorld );

     shared = world->shared;

     D_MAGIC_ASSERT( shared, FusionWorldShared );

     snprintf( buf1, sizeof(buf1), "/dev/fusion%d", shared->world_index );
     snprintf( buf2, sizeof(buf2), "/dev/fusion/%d", shared->world_index );

     /* Open Fusion Kernel Device. */
     fd = direct_try_open( buf1, buf2, O_RDWR /*| O_NONBLOCK*/, true );
     if (fd < 0) {
          D_PERROR( "Fusion/Main: Reopening fusion device (world %d) failed!\n", shared->world_index );
          raise(5);
     }

     /* Drop "identity" when running another program. */
     if (fcntl( fd, F_SETFD, FD_CLOEXEC ) < 0)
          D_PERROR( "Fusion/Init: Setting FD_CLOEXEC flag failed!\n" );

     /* Fill enter information. */
     enter.api.major = FUSION_API_MAJOR_REQUIRED;
     enter.api.minor = FUSION_API_MINOR_REQUIRED;
     enter.fusion_id = 0;     /* Clear for check below. */

     /* Enter the fusion world. */
     while (ioctl( fd, FUSION_ENTER, &enter )) {
          if (errno != EINTR) {
               D_PERROR( "Fusion/Init: Could not reenter world '%d'!\n", shared->world_index );
               raise(5);
          }
     }

     /* Check for valid Fusion ID. */
     if (!enter.fusion_id) {
          D_ERROR( "Fusion/Init: Got no ID from FUSION_ENTER! Kernel module might be too old.\n" );
          raise(5);
     }

     D_DEBUG_AT( Fusion_Main, "  -> Fusion ID 0x%08lx\n", enter.fusion_id );


     /* Fill fork information. */
     fork.fusion_id = world->fusion_id;

     fusion_world_flush_calls( world, 1 );

     /* Fork within the fusion world. */
     while (ioctl( fd, FUSION_FORK, &fork )) {
          if (errno != EINTR) {
               D_PERROR( "Fusion/Main: Could not fork in world '%d'!\n", shared->world_index );
               raise(5);
          }
     }

     D_DEBUG_AT( Fusion_Main, "  -> Fusion ID 0x%08lx\n", fork.fusion_id );

     /* Get new fusion id back. */
     world->fusion_id = fork.fusion_id;

     /* Close old file descriptor. */
     close( world->fusion_fd );

     /* Write back new file descriptor. */
     world->fusion_fd = fd;


     D_DEBUG_AT( Fusion_Main, "  -> restarting dispatcher loop...\n" );

     /* Restart the dispatcher thread. FIXME: free old struct */
     world->dispatch_loop = direct_thread_create( DTT_MESSAGING,
                                                  fusion_dispatch_loop,
                                                  world, "Fusion Dispatch" );
     if (!world->dispatch_loop)
          raise(5);
}

static void
fusion_fork_handler_prepare( void )
{
     int i;
     
     D_DEBUG_AT( Fusion_Main, "%s()\n", __FUNCTION__ );
     
     for (i=0; i<FUSION_MAX_WORLDS; i++) {
          FusionWorld *world = fusion_worlds[i];

          if (!world)
               continue;
               
          D_MAGIC_ASSERT( world, FusionWorld );
          
          if (world->fork_callback)
               world->fork_callback( world->fork_action, FFS_PREPARE );
     }
}     

static void
fusion_fork_handler_parent( void )
{
     int i;

     D_DEBUG_AT( Fusion_Main, "%s()\n", __FUNCTION__ );
    
     for (i=0; i<FUSION_MAX_WORLDS; i++) {
          FusionWorld       *world = fusion_worlds[i];
          FusionWorldShared *shared;

          if (!world)
               continue;
               
          D_MAGIC_ASSERT( world, FusionWorld );
          
          shared = world->shared;
          
          D_MAGIC_ASSERT( shared, FusionWorldShared );
          
          if (world->fork_callback)
               world->fork_callback( world->fork_action, FFS_PARENT );
               
          if (world->fork_action == FFA_FORK) {
               /* Increase the shared reference counter. */
               if (fusion_master( world ))
                    shared->refs++;
          }
     }
}

static void
fusion_fork_handler_child( void )
{
     int i;

     D_DEBUG_AT( Fusion_Main, "%s()\n", __FUNCTION__ );

     for (i=0; i<FUSION_MAX_WORLDS; i++) {
          FusionWorld *world = fusion_worlds[i];

          if (!world)
               continue;

          D_MAGIC_ASSERT( world, FusionWorld );
          
          if (world->fork_callback)
               world->fork_callback( world->fork_action, FFS_CHILD );

          switch (world->fork_action) {
               default:
                    D_BUG( "unknown fork action %d", world->fork_action );

               case FFA_CLOSE:
                    D_DEBUG_AT( Fusion_Main, "  -> closing world %d\n", i );

                    /* Remove world from global list. */
                    fusion_worlds[i] = NULL;

                    /* Unmap shared area. */
                    munmap( world->shared, sizeof(FusionWorldShared) );

                    /* Close Fusion Kernel Device. */
                    close( world->fusion_fd );

                    /* Free local world data. */
                    D_MAGIC_CLEAR( world );
                    D_FREE( world );

                    break;

               case FFA_FORK:
                    D_DEBUG_AT( Fusion_Main, "  -> forking in world %d\n", i );

                    fusion_world_fork( world );

                    break;
          }
     }
}

/**********************************************************************************************************************/

static DirectResult
map_shared_root( void               *shm_base,
                 int                 world_index,
                 bool                master,
                 FusionWorldShared **ret_shared )
{
     DirectResult   ret = DR_OK;
     int            fd;
     void          *map;
     char           tmpfs[FUSION_SHM_TMPFS_PATH_NAME_LEN];
     char           root_file[FUSION_SHM_TMPFS_PATH_NAME_LEN+32];
     int            flags = O_RDONLY;
     int            prot  = PROT_READ;
     unsigned long  size = direct_page_align(sizeof(FusionWorldShared));
     unsigned long  base = (unsigned long) shm_base + (size + direct_pagesize()) * world_index;

     if (master || !fusion_config->secure_fusion) {
          prot  |= PROT_WRITE;
          flags  = O_RDWR;
     }

     if (master)
          flags |= O_CREAT | O_TRUNC;

     if (fusion_config->tmpfs) {
          direct_snputs( tmpfs, fusion_config->tmpfs, FUSION_SHM_TMPFS_PATH_NAME_LEN );
     }
     else if (!fusion_find_tmpfs( tmpfs, FUSION_SHM_TMPFS_PATH_NAME_LEN )) {
          D_ERROR( "Fusion/SHM: Could not find tmpfs mount point, falling back to /dev/shm!\n" );
          direct_snputs( tmpfs, "/dev/shm", FUSION_SHM_TMPFS_PATH_NAME_LEN );
     }

     snprintf( root_file, sizeof(root_file), "%s/fusion.%d", tmpfs, world_index );

     /* open the virtual file */
     fd = open( root_file, flags, 0660 );
     if (fd < 0) {
          ret = errno2result(errno);
          D_PERROR( "Fusion/SHM: Could not open shared memory file '%s'!\n", root_file );
          return ret;
     }

     if (fusion_config->shmfile_gid != (gid_t)-1) {
          /* chgrp the SH_FILE dev entry */
          if (fchown( fd, -1, fusion_config->shmfile_gid ) != 0)
               D_WARN( "Fusion/SHM: Changing owner on %s failed... continuing on.", root_file );
     }

     if (master) {
          fchmod( fd, fusion_config->secure_fusion ? 0640 : 0660 );
          if (ftruncate( fd, size )) {
               ret = errno2result(errno);
               D_PERROR( "Fusion/SHM: Could not truncate shared memory file '%s'!\n", root_file );
               goto out;
          }
     }

     D_DEBUG_AT( Fusion_Main, "  -> mmaping shared memory file... (%zu bytes)\n", sizeof(FusionWorldShared) );



     /* Map shared area. */
     D_INFO( "Fusion/SHM: Shared root (%d) is %zu bytes [0x%lx @ 0x%lx]\n",
             world_index, sizeof(FusionWorldShared), size, base );

     map = mmap( (void*) base, size, prot, MAP_FIXED | MAP_SHARED, fd, 0 );
     if (map == MAP_FAILED) {
          ret = errno2result(errno);
          D_PERROR( "Fusion/Init: Mapping shared area failed!\n" );
          goto out;
     }

     *ret_shared = map;


out:
     close( fd );

     return ret;
}

/**********************************************************************************************************************/

/*
 * Enters a fusion world by joining or creating it.
 *
 * If <b>world</b> is negative, the next free index is used to create a new world.
 * Otherwise the world with the specified index is joined or created.
 */
DirectResult
fusion_enter( int               world_index,
              int               abi_version,
              FusionEnterRole   role,
              FusionWorld     **ret_world )
{
     DirectResult       ret    = DR_OK;
     int                fd     = -1;
     FusionWorld       *world  = NULL;
     FusionWorldShared *shared = NULL;
     FusionEnter        enter;
     char               buf1[20];
     char               buf2[20];

     D_DEBUG_AT( Fusion_Main, "%s( %d, %d, %p )\n", __FUNCTION__, world_index, abi_version, ret_world );

     D_ASSERT( ret_world != NULL );

     if (world_index >= FUSION_MAX_WORLDS) {
          D_ERROR( "Fusion/Init: World index %d exceeds maximum index %d!\n", world_index, FUSION_MAX_WORLDS - 1 );
          return DR_INVARG;
     }

     pthread_once( &fusion_init_once, init_once );


     if (fusion_config->force_slave)
          role = FER_SLAVE;

     direct_initialize();

     pthread_mutex_lock( &fusion_worlds_lock );


     if (world_index < 0) {
          if (role == FER_SLAVE) {
               D_ERROR( "Fusion/Init: Slave role and a new world (index -1) was requested!\n" );
               pthread_mutex_unlock( &fusion_worlds_lock );
               return DR_INVARG;
          }

          for (world_index=0; world_index<FUSION_MAX_WORLDS; world_index++) {
               world = fusion_worlds[world_index];
               if (world)
                    break;

               snprintf( buf1, sizeof(buf1), "/dev/fusion%d", world_index );
               snprintf( buf2, sizeof(buf2), "/dev/fusion/%d", world_index );

               /* Open Fusion Kernel Device. */
               fd = direct_try_open( buf1, buf2, O_RDWR/*| O_NONBLOCK*/ | O_EXCL, false );
               if (fd < 0) {
                    if (errno != EBUSY)
                         D_PERROR( "Fusion/Init: Error opening '%s' and/or '%s'!\n", buf1, buf2 );
               }
               else
                    break;
          }
     }
     else {
          world = fusion_worlds[world_index];
          if (!world) {
               int flags = O_RDWR/*| O_NONBLOCK*/;

               snprintf( buf1, sizeof(buf1), "/dev/fusion%d", world_index );
               snprintf( buf2, sizeof(buf2), "/dev/fusion/%d", world_index );

               if (role == FER_MASTER)
                    flags |= O_EXCL;
               else if (role == FER_SLAVE)
                    flags |= O_APPEND;

               /* Open Fusion Kernel Device. */
               fd = direct_try_open( buf1, buf2, flags, true );
          }
     }

     /* Enter a world again? */
     if (world) {
          D_MAGIC_ASSERT( world, FusionWorld );
          D_ASSERT( world->refs > 0 );

          /* Check the role again. */
          switch (role) {
               case FER_MASTER:
                    if (world->fusion_id != FUSION_ID_MASTER) {
                         D_ERROR( "Fusion/Init: Master role requested for a world (%d) "
                                  "we're already slave in!\n", world_index );
                         ret = DR_UNSUPPORTED;
                         goto error;
                    }
                    break;

               case FER_SLAVE:
                    if (world->fusion_id == FUSION_ID_MASTER) {
                         D_ERROR( "Fusion/Init: Slave role requested for a world (%d) "
                                  "we're already master in!\n", world_index );
                         ret = DR_UNSUPPORTED;
                         goto error;
                    }
                    break;

               case FER_ANY:
                    break;
          }

          shared = world->shared;

          D_MAGIC_ASSERT( shared, FusionWorldShared );

          if (shared->world_abi != abi_version) {
               D_ERROR( "Fusion/Init: World ABI (%d) of world '%d' doesn't match own (%d)!\n",
                        shared->world_abi, world_index, abi_version );
               ret = DR_VERSIONMISMATCH;
               goto error;
          }

          world->refs++;

          pthread_mutex_unlock( &fusion_worlds_lock );

          D_DEBUG_AT( Fusion_Main, "  -> using existing world %p [%d]\n", world, world_index );

          /* Return the world. */
          *ret_world = world;

          return DR_OK;
     }

     if (fd < 0) {
          D_PERROR( "Fusion/Init: Opening fusion device (world %d) as '%s' failed!\n", world_index,
                    role == FER_ANY ? "any" : (role == FER_MASTER ? "master" : "slave")  );
          ret = DR_INIT;
          goto error;
     }

     /* Drop "identity" when running another program. */
     if (fcntl( fd, F_SETFD, FD_CLOEXEC ) < 0)
          D_PERROR( "Fusion/Init: Setting FD_CLOEXEC flag failed!\n" );

     /* Fill enter information. */
     enter.api.major = FUSION_API_MAJOR_REQUIRED;
     enter.api.minor = FUSION_API_MINOR_REQUIRED;
     enter.fusion_id = 0;     /* Clear for check below. */
     enter.secure    = fusion_config->secure_fusion;

     /* Enter the fusion world. */
     while (ioctl( fd, FUSION_ENTER, &enter )) {
          if (errno != EINTR) {
               D_PERROR( "Fusion/Init: Could not enter world '%d'!\n", world_index );
               ret = DR_INIT;
               goto error;
          }
     }

     /* Check for valid Fusion ID. */
     if (!enter.fusion_id) {
          D_ERROR( "Fusion/Init: Got no ID from FUSION_ENTER! Kernel module might be too old.\n" );
          ret = DR_INIT;
          goto error;
     }

     D_DEBUG_AT( Fusion_Main, "  -> Fusion ID 0x%08lx\n", enter.fusion_id );

     /* Check slave role only, master is handled by O_EXCL earlier. */
     if (role == FER_SLAVE && enter.fusion_id == FUSION_ID_MASTER) {
          D_PERROR( "Fusion/Init: Entering world '%d' as a slave failed!\n", world_index );
          ret = DR_UNSUPPORTED;
          goto error;
     }


     unsigned long shm_base;

     if (ioctl( fd, FUSION_SHM_GET_BASE, &shm_base )) {
          ret = errno2result(errno);
          D_PERROR( "Fusion/Init: FUSION_SHM_GET_BASE failed!\n" );
          goto error;
     }


     /* Map shared area. */
     ret = map_shared_root( (void*) shm_base, world_index, enter.fusion_id == FUSION_ID_MASTER, &shared );
     if (ret)
          goto error;

     D_DEBUG_AT( Fusion_Main, "  -> shared area at %p, size %zu\n", shared, sizeof(FusionWorldShared) );

     /* Initialize shared data. */
     if (enter.fusion_id == FUSION_ID_MASTER) {
          /* Initialize reference counter. */
          shared->refs = 1;
          
          /* Set ABI version. */
          shared->world_abi = abi_version;

          /* Set the world index. */
          shared->world_index = world_index;

          /* Set start time of world clock. */
          shared->start_time = direct_clock_get_time( DIRECT_CLOCK_SESSION );

          D_MAGIC_SET( shared, FusionWorldShared );
     }
     else {
          D_MAGIC_ASSERT( shared, FusionWorldShared );

          /* Check ABI version. */
          if (shared->world_abi != abi_version) {
               D_ERROR( "Fusion/Init: World ABI (%d) doesn't match own (%d)!\n",
                        shared->world_abi, abi_version );
               ret = DR_VERSIONMISMATCH;
               goto error;
          }
     }

     /* Synchronize to world clock. */
     direct_clock_set_time( DIRECT_CLOCK_SESSION, shared->start_time );
     

     /* Allocate local data. */
     world = D_CALLOC( 1, sizeof(FusionWorld) );
     if (!world) {
          ret = D_OOM();
          goto error;
     }

     /* Initialize local data. */
     world->refs      = 1;
     world->shared    = shared;
     world->fusion_fd = fd;
     world->fusion_id = enter.fusion_id;

     direct_mutex_init( &world->reactor_nodes_lock );

     D_MAGIC_SET( world, FusionWorld );

     fusion_worlds[world_index] = world;

     /* Initialize shared memory part. */
     ret = fusion_shm_init( world );
     if (ret)
          goto error2;


     D_DEBUG_AT( Fusion_Main, "  -> initializing other parts...\n" );

     direct_mutex_init( &world->refs_lock );

     /* Initialize other parts. */
     if (enter.fusion_id == FUSION_ID_MASTER) {
          fusion_skirmish_init2( &shared->reactor_globals, "Fusion Reactor Globals", world, fusion_config->secure_fusion );
          fusion_skirmish_init2( &shared->arenas_lock, "Fusion Arenas", world, fusion_config->secure_fusion );

          if (!fusion_config->secure_fusion) {
               fusion_skirmish_add_permissions( &shared->reactor_globals, 0,
                                                FUSION_SKIRMISH_PERMIT_PREVAIL
                                                | FUSION_SKIRMISH_PERMIT_DISMISS );
               fusion_skirmish_add_permissions( &shared->arenas_lock, 0,
                                                FUSION_SKIRMISH_PERMIT_PREVAIL
                                                | FUSION_SKIRMISH_PERMIT_DISMISS );
          }

          /* Create the main pool. */
          ret = fusion_shm_pool_create( world, "Fusion Main Pool", 0x1000000,
                                        fusion_config->debugshm, &shared->main_pool );
          if (ret)
               goto error3;

          fusion_call_init( &shared->refs_call, world_refs_call, world, world );
          fusion_call_set_name( &shared->refs_call, "world_refs" );
          fusion_call_add_permissions( &shared->refs_call, 0, FUSION_CALL_PERMIT_EXECUTE );

          direct_map_create( 37, refs_map_compare, refs_map_hash, world, &world->refs_map );
     }
     else
          direct_map_create( 37, refs_map_slave_compare, refs_map_slave_hash, world, &world->refs_map );

     D_DEBUG_AT( Fusion_Main, "  -> starting dispatcher loop...\n" );

     /* Start the dispatcher thread. */
     world->dispatch_loop = direct_thread_create( DTT_MESSAGING,
                                                  fusion_dispatch_loop,
                                                  world, "Fusion Dispatch" );
     if (!world->dispatch_loop) {
          ret = DR_FAILURE;
          goto error4;
     }

     pthread_cond_init( &world->deferred.queue, NULL );
     pthread_mutex_init( &world->deferred.lock, NULL );

     /* Start the deferred thread. */
     world->deferred.thread = direct_thread_create( DTT_MESSAGING,
                                                    fusion_deferred_loop,
                                                    world, "Fusion Deferred" );
     if (!world->deferred.thread) {
          ret = DR_FAILURE;
          goto error4;
     }

     D_DEBUG_AT( Fusion_Main, "  -> done. (%p)\n", world );

     pthread_mutex_unlock( &fusion_worlds_lock );

     /* Return the fusion world. */
     *ret_world = world;

     return DR_OK;


error4:
     if (world->deferred.thread)
          direct_thread_destroy( world->deferred.thread );

     if (world->dispatch_loop)
          direct_thread_destroy( world->dispatch_loop );

     if (enter.fusion_id == FUSION_ID_MASTER)
          fusion_shm_pool_destroy( world, shared->main_pool );

error3:
     if (enter.fusion_id == FUSION_ID_MASTER) {
          fusion_skirmish_destroy( &shared->arenas_lock );
          fusion_skirmish_destroy( &shared->reactor_globals );
     }

     fusion_shm_deinit( world );


error2:
     fusion_worlds[world_index] = world;

     D_MAGIC_CLEAR( world );

     D_FREE( world );

error:
     if (shared && shared != MAP_FAILED) {
          if (enter.fusion_id == FUSION_ID_MASTER)
               D_MAGIC_CLEAR( shared );

          munmap( shared, sizeof(FusionWorldShared) );
     }

     if (fd != -1)
          close( fd );

     pthread_mutex_unlock( &fusion_worlds_lock );

     direct_shutdown();

     return ret;
}

DirectResult
fusion_world_activate( FusionWorld *world )
{
     D_DEBUG_AT( Fusion_Main, "  -> unblocking world...\n" );

     while (ioctl( world->fusion_fd, FUSION_UNBLOCK )) {
          if (errno != EINTR) {
               D_PERROR( "Fusion/Init: Could not unblock world!\n" );
               return DR_FUSION;
          }
     }

     return DR_OK;
}

DirectResult
fusion_stop_dispatcher( FusionWorld *world,
                        bool         emergency )
{
     D_DEBUG_AT( Fusion_Main_Dispatch, "%s( %semergency )\n", __FUNCTION__, emergency ? "" : "no " );

     if (!world->dispatch_loop)
          return DR_OK;

     if (!emergency) {
          fusion_sync( world );

          D_DEBUG_AT( Fusion_Main_Dispatch, "  -> locking thread...\n" );

          direct_thread_lock( world->dispatch_loop );
     }

     D_DEBUG_AT( Fusion_Main_Dispatch, "  -> locked\n" );

     world->dispatch_stop = true;

     if (!emergency) {
          D_DEBUG_AT( Fusion_Main_Dispatch, "  -> unlocking thread...\n" );

          direct_thread_unlock( world->dispatch_loop );

          fusion_sync( world );
     }

     fcntl( world->fusion_fd, F_SETFL, O_NONBLOCK );

     D_DEBUG_AT( Fusion_Main_Dispatch, "  -> finished stopping.\n" );

     return DR_OK;
}

/*
 * Exits the fusion world.
 *
 * If 'emergency' is true the function won't join but kill the dispatcher thread.
 */
DirectResult
fusion_exit( FusionWorld *world,
             bool         emergency )
{
     FusionWorldShared *shared;

     D_DEBUG_AT( Fusion_Main, "%s( %p, %semergency )\n", __FUNCTION__, world, emergency ? "" : "no " );

     D_MAGIC_ASSERT( world, FusionWorld );

     shared = world->shared;

     D_MAGIC_ASSERT( shared, FusionWorldShared );

     pthread_mutex_lock( &fusion_worlds_lock );

     D_ASSERT( world->refs > 0 );

     if (--world->refs) {
          pthread_mutex_unlock( &fusion_worlds_lock );
          return DR_OK;
     }

     D_ASSUME( direct_thread_self() != world->dispatch_loop );

     if (direct_thread_self() != world->dispatch_loop) {
          int               foo;
          FusionSendMessage msg;

          /* Wake up the read loop thread. */
          msg.fusion_id = world->fusion_id;
          msg.msg_id    = 0;
          msg.msg_data  = &foo;
          msg.msg_size  = sizeof(foo);

          fusion_world_flush_calls( world, 1 );

          while (ioctl( world->fusion_fd, FUSION_SEND_MESSAGE, &msg ) < 0) {
               if (errno != EINTR) {
                    D_PERROR( "FUSION_SEND_MESSAGE" );
                    direct_thread_cancel( world->dispatch_loop );
                    break;
               }
          }

          /* Wait for its termination. */
          direct_thread_join( world->dispatch_loop );
     }

     D_ASSUME( direct_thread_self() != world->deferred.thread );

     /* Wake up the deferred call thread. */
     pthread_cond_signal( &world->deferred.queue );

     /* Wait for its termination. */
     direct_thread_join( world->deferred.thread );

     direct_thread_destroy( world->dispatch_loop );
     direct_thread_destroy( world->deferred.thread );

     pthread_mutex_destroy( &world->deferred.lock );
     pthread_cond_destroy( &world->deferred.queue );

     direct_mutex_deinit( &world->refs_lock );
     direct_map_destroy( world->refs_map );

     /* Master has to deinitialize shared data. */
     if (fusion_master( world )) {
          fusion_call_destroy( &shared->refs_call );

          shared->refs--;
          if (shared->refs == 0) {
               fusion_skirmish_destroy( &shared->reactor_globals );
               fusion_skirmish_destroy( &shared->arenas_lock );

               fusion_shm_pool_destroy( world, shared->main_pool );
          
               /* Deinitialize shared memory. */
               fusion_shm_deinit( world );
          }
     }
     else {
          /* Leave shared memory. */
          fusion_shm_deinit( world );
     }

     /* Reset local dispatch nodes. */
     _fusion_reactor_free_all( world );


     /* Remove world from global list. */
     fusion_worlds[shared->world_index] = NULL;


     /* Unmap shared area. */
     if (fusion_master( world ) && shared->refs == 0) {
          char         tmpfs[FUSION_SHM_TMPFS_PATH_NAME_LEN];
          char         root_file[FUSION_SHM_TMPFS_PATH_NAME_LEN+32];

          if (fusion_config->tmpfs)
               direct_snputs( tmpfs, fusion_config->tmpfs, FUSION_SHM_TMPFS_PATH_NAME_LEN );
          else if (!fusion_find_tmpfs( tmpfs, FUSION_SHM_TMPFS_PATH_NAME_LEN ))
               direct_snputs( tmpfs, "/dev/shm", FUSION_SHM_TMPFS_PATH_NAME_LEN );

          snprintf( root_file, sizeof(root_file), "%s/fusion.%d", tmpfs, shared->world_index );

          if (unlink( root_file ) < 0)
               D_PERROR( "Fusion/Main: could not unlink shared memory file (%s)!\n", root_file );

          D_MAGIC_CLEAR( shared );
     }

     munmap( shared, sizeof(FusionWorldShared) );


     /* Close Fusion Kernel Device. */
     close( world->fusion_fd );


     /* Free local world data. */
     D_MAGIC_CLEAR( world );

     D_FREE( world );


     pthread_mutex_unlock( &fusion_worlds_lock );

     direct_shutdown();

     return DR_OK;
}

/*
 * Sends a signal to one or more fusionees and optionally waits
 * for their processes to terminate.
 *
 * A fusion_id of zero means all fusionees but the calling one.
 * A timeout of zero means infinite waiting while a negative value
 * means no waiting at all.
 */
DirectResult
fusion_kill( FusionWorld *world,
             FusionID     fusion_id,
             int          signal,
             int          timeout_ms )
{
     FusionKill param;

     D_MAGIC_ASSERT( world, FusionWorld );

     param.fusion_id  = fusion_id;
     param.signal     = signal;
     param.timeout_ms = timeout_ms;

     fusion_world_flush_calls( world, 1 );

     while (ioctl( world->fusion_fd, FUSION_KILL, &param )) {
          switch (errno) {
               case EINTR:
                    continue;
               case ETIMEDOUT:
                    return DR_TIMEOUT;
               default:
                    break;
          }

          D_PERROR ("FUSION_KILL");

          return DR_FAILURE;
     }

     return DR_OK;
}

const char *
fusion_get_tmpfs( FusionWorld *world )
{
     D_MAGIC_ASSERT( world, FusionWorld );
     D_MAGIC_ASSERT( world->shared, FusionWorldShared );

     return world->shared->shm.tmpfs;
}

/**********************************************************************************************************************/

static DirectResult
defer_message( FusionWorld       *world,
               FusionReadMessage *header,
               void              *data )
{
     DeferredCall *deferred;

     deferred = D_CALLOC( 1, sizeof(DeferredCall) + header->msg_size );
     if (!deferred)
          return D_OOM();

     deferred->header = *header;

     direct_memcpy( deferred + 1, data, header->msg_size );

     pthread_mutex_lock( &world->deferred.lock );

     direct_list_append( &world->deferred.list, &deferred->link );

     pthread_mutex_unlock( &world->deferred.lock );

     pthread_cond_signal( &world->deferred.queue );

     return DR_OK;
}

DirectResult
fusion_dispatch_cleanup_add( FusionWorld                  *world,
                             FusionDispatchCleanupFunc     func,
                             void                         *ctx,
                             FusionDispatchCleanup       **ret_cleanup )
{
     FusionDispatchCleanup *cleanup;

     cleanup = D_CALLOC( 1, sizeof(FusionDispatchCleanup) );
     if (!cleanup)
          return D_OOM();

     cleanup->func = func;
     cleanup->ctx  = ctx;

     direct_list_append( &world->dispatch_cleanups, &cleanup->link );

     *ret_cleanup = cleanup;

     return DR_OK;
}

DirectResult
fusion_dispatch_cleanup_remove( FusionWorld                  *world,
                                FusionDispatchCleanup        *cleanup )
{
     direct_list_remove( &world->dispatch_cleanups, &cleanup->link );

     D_FREE( cleanup );

     return DR_OK;
}

static void
handle_dispatch_cleanups( FusionWorld *world )
{
     FusionDispatchCleanup *cleanup, *next;

     D_DEBUG_AT( Fusion_Main_Dispatch, "%s( %p )\n", __FUNCTION__, world );

     direct_list_foreach_safe (cleanup, next, world->dispatch_cleanups) {
#if D_DEBUG_ENABLED
          if (direct_log_domain_check( &Fusion_Main_Dispatch )) // avoid call to direct_trace_lookup_symbol_at
               D_DEBUG_AT( Fusion_Main_Dispatch, "  -> %s (%p)\n", direct_trace_lookup_symbol_at( cleanup->func ), cleanup->ctx );
#endif

          cleanup->func( cleanup->ctx );

          D_FREE( cleanup );
     }

     D_DEBUG_AT( Fusion_Main_Dispatch, "  -> cleanups done.\n" );

     world->dispatch_cleanups = NULL;
}

static DirectEnumerationResult
refs_iterate( DirectMap    *map,
              void         *object,
              void         *ctx )
{
     FusionRefSlaveEntry *entry = object;

     if (entry->key.fusion_id == *((FusionID*)ctx)) {
          int i;

          for (i=0; i<entry->refs; i++)
               fusion_ref_down( entry->ref, false );

          D_FREE( entry );

          return DENUM_REMOVE;
     }

     return DENUM_OK;
}

static void *
fusion_dispatch_loop( DirectThread *thread, void *arg )
{
     ssize_t      len      = 0;
     size_t       buf_size = FUSION_MESSAGE_SIZE * 4;
     char        *buf      = malloc( buf_size );
     FusionWorld *world    = arg;

     D_DEBUG_AT( Fusion_Main_Dispatch, "%s() running...\n", __FUNCTION__ );

     direct_thread_lock( thread );

     while (true) {
          char *buf_p = buf;

          D_MAGIC_ASSERT( world, FusionWorld );

          if (world->dispatch_stop) {
               D_DEBUG_AT( Fusion_Main_Dispatch, "  -> IGNORING (dispatch_stop!)\n" );

               goto out;
          }
          else {
               D_DEBUG_AT( Fusion_Main_Dispatch, "%s( world %p ) ==> read( %zu )...\n", __FUNCTION__, world, buf_size );

               direct_thread_unlock( thread );

               len = read( world->fusion_fd, buf, buf_size );

               direct_thread_lock( thread );

               if (len < 0) {
                    if (errno == EINTR)
                         continue;

                    break;
               }

               D_DEBUG_AT( Fusion_Main_Dispatch, "%s( world %p ) ==> got %zu (of up to %zu)\n", __FUNCTION__, world, len, buf_size );

               while (buf_p < buf + len) {
                    FusionReadMessage *header = (FusionReadMessage*) buf_p;
                    void              *data   = buf_p + sizeof(FusionReadMessage);

                    D_DEBUG_AT( Fusion_Main_Dispatch, "%s( world %p ) ==> %p [%ld]\n",
                                __FUNCTION__, world, header, (long) buf_p - (long) buf );

//                    if (world->dispatch_stop) {
//                         D_DEBUG_AT( Fusion_Main_Dispatch, "  -> ABORTING (dispatch_stop!)\n" );
//                         break;
//                    }

                    D_MAGIC_ASSERT( world, FusionWorld );
                    D_ASSERT( (buf + len - buf_p) >= sizeof(FusionReadMessage) );

                    switch (header->msg_type) {
                         case FMT_SEND:
                              D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_SEND!\n" );
                              break; 
                         case FMT_CALL:
                              D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_CALL...\n" );

                              if (((FusionCallMessage*) data)->caller == 0)
                                   handle_dispatch_cleanups( world );

                              /* If the call comes from kernel space it is most likely a destructor call, defer it */
                              if (fusion_config->defer_destructors && ((FusionCallMessage*) data)->caller == 0) {
                                   defer_message( world, header, data );
                              }
                              else
                                   _fusion_call_process( world, header->msg_id, data,
                                                         (header->msg_size != sizeof(FusionCallMessage))
                                                         ?data + sizeof(FusionCallMessage) : NULL );
                              break;
                         case FMT_REACTOR:
                              D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_REACTOR...\n" );
                              //defer_message( world, header, data );
                              _fusion_reactor_process_message( world, header->msg_id, header->msg_channel, data );
                              break;
                         case FMT_SHMPOOL:
                              D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_SHMPOOL...\n" );
                              //defer_message( world, header, data );
                              _fusion_shmpool_process( world, header->msg_id, data );
                              break;
                         case FMT_CALL3:
                              D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_CALL3...\n" );
                              //defer_message( world, header, data );
                              _fusion_call_process3( world, header->msg_id, data,
                                                     (header->msg_size != sizeof(FusionCallMessage3))
                                                     ?data + sizeof(FusionCallMessage3) : NULL );
                              break;
                         case FMT_LEAVE:
                              D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_LEAVE...\n" );

                              if (world->fusion_id == FUSION_ID_MASTER) {
                                   direct_mutex_lock( &world->refs_lock );
                                   direct_map_iterate( world->refs_map, refs_iterate, data );
                                   direct_mutex_unlock( &world->refs_lock );
                              }

                              if (world->leave_callback)
                                   world->leave_callback( world, *((FusionID*)data), world->leave_ctx );
                              break;
                         default:
                              D_DEBUG( "Fusion/Receiver: discarding message of unknown type '%d'\n",
                                       header->msg_type );
                              break;
                    }

                    D_DEBUG_AT( Fusion_Main_Dispatch, "  -> done.\n" );

                    buf_p = data + ((header->msg_size + 3) & ~3);
               }
          }

          handle_dispatch_cleanups( world );

          if (!world->refs) {
               D_DEBUG_AT( Fusion_Main_Dispatch, "  -> good bye!\n" );
               goto out;
          }
          else {
//               direct_thread_unlock( thread );
//               direct_thread_sleep( 5000000 );
//               direct_thread_lock( thread );
          }
     }

     D_PERROR( "Fusion/Receiver: reading from fusion device failed!\n" );

out:
     direct_thread_unlock( thread );
     free( buf );
     return NULL;
}

DirectResult
fusion_dispatch( FusionWorld *world,
                 size_t       buf_size )
{
     ssize_t  len = 0;
     char    *buf;
     char    *buf_p;

     D_DEBUG_AT( Fusion_Main_Dispatch, "%s( world %p, buf_size %zu )\n", __FUNCTION__, world, buf_size );

     D_MAGIC_ASSERT( world, FusionWorld );

     if (buf_size == 0)
          buf_size = FUSION_MESSAGE_SIZE * 4;
     else
          D_ASSUME( buf_size >= FUSION_MESSAGE_SIZE );

     buf = buf_p = malloc( buf_size );

//     if (world->dispatch_stop) {
//          D_DEBUG_AT( Fusion_Main_Dispatch, "  -> IGNORING (dispatch_stop!)\n" );
//          return DR_SUSPENDED;
//     }

     D_DEBUG_AT( Fusion_Main_Dispatch, "  = = dispatch -> reading up to %zu bytes...\n", buf_size );

     while (true) {
          len = read( world->fusion_fd, buf, buf_size );
          if (len < 0) {
               if (errno == EINTR)
                    continue;

               if (errno != EAGAIN)
                    D_PERROR( "Fusion/Receiver: reading from fusion device failed!\n" );

               free( buf );
               return DR_IO;
          }

          break;
     }

     D_DEBUG_AT( Fusion_Main_Dispatch, "  = = dispatch -> got %zu bytes (of up to %zu)\n", len, buf_size );

     if (world->dispatch_loop)
          direct_thread_lock( world->dispatch_loop );

     while (buf_p < buf + len) {
          FusionReadMessage *header = (FusionReadMessage*) buf_p;
          void              *data   = buf_p + sizeof(FusionReadMessage);

          D_DEBUG_AT( Fusion_Main_Dispatch, "  = = dispatch -> %p [%ld]\n", header, (long) buf_p - (long) buf );

//          if (world->dispatch_stop) {
//               D_DEBUG_AT( Fusion_Main_Dispatch, "  -> ABORTING (dispatch_stop!)\n" );
//               break;
//          }

          D_MAGIC_ASSERT( world, FusionWorld );
          D_ASSERT( (buf + len - buf_p) >= sizeof(FusionReadMessage) );

          switch (header->msg_type) {
               case FMT_SEND:
                    D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_SEND!\n" );
                    break; 
               case FMT_CALL:
                    D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_CALL...\n" );

                    if (((FusionCallMessage*) data)->caller == 0)
                         handle_dispatch_cleanups( world );

                    /* If the call comes from kernel space it is most likely a destructor call, defer it */
                    if (fusion_config->defer_destructors && ((FusionCallMessage*) data)->caller == 0) {
                         defer_message( world, header, data );
                    }
                    else
                         _fusion_call_process( world, header->msg_id, data,
                                               (header->msg_size != sizeof(FusionCallMessage))
                                               ?data + sizeof(FusionCallMessage) : NULL );
                    break;
               case FMT_REACTOR:
                    D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_REACTOR...\n" );
                    //defer_message( world, header, data );
                    _fusion_reactor_process_message( world, header->msg_id, header->msg_channel, data );
                    break;
               case FMT_SHMPOOL:
                    D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_SHMPOOL...\n" );
                    //defer_message( world, header, data );
                    _fusion_shmpool_process( world, header->msg_id, data );
                    break;
               case FMT_CALL3:
                    D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_CALL3...\n" );
                    //defer_message( world, header, data );
                    _fusion_call_process3( world, header->msg_id, data,
                                           (header->msg_size != sizeof(FusionCallMessage3))
                                           ?data + sizeof(FusionCallMessage3) : NULL );
                    break;
               case FMT_LEAVE:
                    D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_LEAVE...\n" );

                    if (world->fusion_id == FUSION_ID_MASTER) {
                         direct_mutex_lock( &world->refs_lock );
                         direct_map_iterate( world->refs_map, refs_iterate, data );
                         direct_mutex_unlock( &world->refs_lock );
                    }

                    if (world->leave_callback)
                         world->leave_callback( world, *((FusionID*)data), world->leave_ctx );
                    break;
               default:
                    D_DEBUG( "Fusion/Receiver: discarding message of unknown type '%d'\n",
                             header->msg_type );
                    break;
          }

          D_DEBUG_AT( Fusion_Main_Dispatch, "  -> done.\n" );

          buf_p = data + ((header->msg_size + 3) & ~3);
     }

     handle_dispatch_cleanups( world );

     if (world->dispatch_loop)
          direct_thread_unlock( world->dispatch_loop );

     free( buf );

     return DR_OK;
}

static void *
fusion_deferred_loop( DirectThread *thread, void *arg )
{
     FusionWorld *world = arg;

     D_DEBUG_AT( Fusion_Main_Dispatch, "%s() running...\n", __FUNCTION__ );

     pthread_mutex_lock( &world->deferred.lock );

     while (world->refs) {
          DeferredCall *deferred;

          D_MAGIC_ASSERT( world, FusionWorld );

          deferred = (DeferredCall*) world->deferred.list;
          if (!deferred) {
               pthread_cond_wait( &world->deferred.queue, &world->deferred.lock );

               continue;
          }

          direct_list_remove( &world->deferred.list, &deferred->link );

          pthread_mutex_unlock( &world->deferred.lock );


          FusionReadMessage *header = &deferred->header;
          void              *data   = header + 1;

          switch (header->msg_type) {
               case FMT_SEND:
                    D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_SEND!\n" );
                    break; 
               case FMT_CALL:
                    D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_CALL...\n" );
                    _fusion_call_process( world, header->msg_id, data,
                                          (header->msg_size != sizeof(FusionCallMessage))
                                             ? data + sizeof(FusionCallMessage) : NULL );
                    break;
               case FMT_REACTOR:
                    D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_REACTOR...\n" );
                    _fusion_reactor_process_message( world, header->msg_id, header->msg_channel, data );
                    break;
               case FMT_SHMPOOL:
                    D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_SHMPOOL...\n" );
                    _fusion_shmpool_process( world, header->msg_id, data );
                    break;
               case FMT_CALL3:
                    D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_CALL3...\n" );

                    _fusion_call_process3( world, header->msg_id, data,
                                           (header->msg_size != sizeof(FusionCallMessage3))
                                           ?data + sizeof(FusionCallMessage3) : NULL );
                    break;
               default:
                    D_DEBUG( "Fusion/Receiver: discarding message of unknown type '%d'\n",
                             header->msg_type );
                    break;
          }

          D_FREE( deferred );

          pthread_mutex_lock( &world->deferred.lock );
     }

     pthread_mutex_unlock( &world->deferred.lock );

     return NULL;
}

/**********************************************************************************************************************/

DirectResult
fusion_get_fusionee_path( const FusionWorld *world,
                          FusionID           fusion_id,
                          char              *buf,
                          size_t             buf_size,
                          size_t            *ret_size )
{
     FusionGetFusioneeInfo info;
     size_t                len;

     D_ASSERT( world != NULL );
     D_ASSERT( buf != NULL );
     D_ASSERT( ret_size != NULL );

     info.fusion_id = fusion_id;

     while (ioctl( world->fusion_fd, FUSION_GET_FUSIONEE_INFO, &info ) < 0) {
          switch (errno) {
               case EINTR:
                    continue;

               default:
                    break;
          }

          D_PERROR( "Fusion: FUSION_GET_FUSIONEE_INFO failed!\n" );

          return DR_FUSION;
     }

     len = strlen( info.exe_file ) + 1;

     if (len > buf_size) {
          *ret_size = len;
          return DR_LIMITEXCEEDED;
     }

     direct_memcpy( buf, info.exe_file, len );

     *ret_size = len;

     return DR_OK;
}

DirectResult
fusion_get_fusionee_pid( const FusionWorld *world,
                         FusionID           fusion_id,
                         pid_t             *ret_pid )
{
     FusionGetFusioneeInfo info;

     D_ASSERT( world != NULL );
     D_ASSERT( ret_pid != NULL );

     info.fusion_id = fusion_id;

     while (ioctl( world->fusion_fd, FUSION_GET_FUSIONEE_INFO, &info ) < 0) {
          switch (errno) {
               case EINTR:
                    continue;

               default:
                    break;
          }

          D_PERROR( "Fusion: FUSION_GET_FUSIONEE_INFO failed!\n" );

          return DR_FUSION;
     }

     *ret_pid = info.pid;

     return DR_OK;
}

DirectResult
fusion_world_set_root( FusionWorld *world,
                       void        *root )
{
     D_ASSERT( world != NULL );
     D_ASSERT( world->shared != NULL );

     if (world->fusion_id != FUSION_ID_MASTER)
          return DR_ACCESSDENIED;

     world->shared->world_root = root;

     return DR_OK;
}

void *
fusion_world_get_root( FusionWorld *world )
{
     D_ASSERT( world != NULL );
     D_ASSERT( world->shared != NULL );

     return world->shared->world_root;
}

/*
 * Wait until all pending messages are processed.
 */
DirectResult
fusion_sync( const FusionWorld *world )
{
     D_MAGIC_ASSERT( world, FusionWorld );

     D_DEBUG_AT( Fusion_Main, "%s( %p )\n", __FUNCTION__, world );

     D_DEBUG_AT( Fusion_Main, "  -> syncing with fusion device...\n" );

     while (ioctl( world->fusion_fd, FUSION_SYNC )) {
          switch (errno) {
               case EINTR:
                    continue;
               default:
                    break;
          }

          D_PERROR( "Fusion/Main: FUSION_SYNC failed!\n" );

          return DR_FAILURE;
     }

     D_DEBUG_AT( Fusion_Main, "  -> synced\n");

     return DR_OK;
}

#else /* FUSION_BUILD_KERNEL */

#include <dirent.h>

#include <direct/system.h>

typedef struct {
     DirectLink   link;

     FusionRef   *ref;

     int          count;
} __FusioneeRef;

typedef struct {
     DirectLink   link;
     
     FusionID     id;
     pid_t        pid;

     DirectLink  *refs;
} __Fusionee;


/**********************************************************************************************************************/

static DirectResult
_fusion_add_fusionee( FusionWorld *world, FusionID fusion_id )
{
     DirectResult       ret;
     FusionWorldShared *shared;
     __Fusionee        *fusionee;

     D_DEBUG_AT( Fusion_Main, "%s( %p, %lu )\n", __FUNCTION__, world, fusion_id );

     D_MAGIC_ASSERT( world, FusionWorld );

     shared = world->shared;

     D_MAGIC_ASSERT( shared, FusionWorldShared );

     fusionee = SHCALLOC( shared->main_pool, 1, sizeof(__Fusionee) );
     if (!fusionee)
          return D_OOSHM();
     
     fusionee->id  = fusion_id;
     fusionee->pid = direct_gettid();
     
     ret = fusion_skirmish_prevail( &shared->fusionees_lock );
     if (ret) {
          SHFREE( shared->main_pool, fusionee );
          return ret;
     }
     
     direct_list_append( &shared->fusionees, &fusionee->link );
     
     fusion_skirmish_dismiss( &shared->fusionees_lock );

     /* Set local pointer. */
     world->fusionee = fusionee;

     return DR_OK;
}    

void
_fusion_add_local( FusionWorld *world, FusionRef *ref, int add )
{
     FusionWorldShared *shared;
     __Fusionee        *fusionee;
     __FusioneeRef     *fusionee_ref;

     //D_DEBUG_AT( Fusion_Main, "%s( %p, %p, %d )\n", __FUNCTION__, world, ref, add );

     D_ASSERT( ref != NULL );
     D_MAGIC_ASSERT( world, FusionWorld );

     shared = world->shared;
     D_MAGIC_ASSERT( shared, FusionWorldShared );

     fusionee = world->fusionee;
     D_ASSERT( fusionee != NULL );

     direct_list_foreach (fusionee_ref, fusionee->refs) {
          if (fusionee_ref->ref == ref)
               break;
     }

     if (fusionee_ref) { 
          fusionee_ref->count += add;

          //D_DEBUG_AT( Fusion_Main, " -> refs = %d\n", fusionee_ref->count );
          
          if (fusionee_ref->count == 0) {
               direct_list_remove( &fusionee->refs, &fusionee_ref->link );

               SHFREE( shared->main_pool, fusionee_ref );
          }
     }
     else {
          if (add <= 0) /* called from _fusion_remove_fusionee() */
               return;
          
          //D_DEBUG_AT( Fusion_Main, " -> new ref\n" );
          
          fusionee_ref = SHCALLOC( shared->main_pool, 1, sizeof(__FusioneeRef) );
          if (!fusionee_ref) {
               D_OOSHM();
               return;
          }

          fusionee_ref->ref   = ref;
          fusionee_ref->count = add;

          direct_list_prepend( &fusionee->refs, &fusionee_ref->link );
     }
}

void
_fusion_check_locals( FusionWorld *world, FusionRef *ref )
{
     FusionWorldShared *shared;
     __Fusionee        *fusionee;
     __FusioneeRef     *fusionee_ref, *temp;
     DirectLink        *list = NULL;

     D_DEBUG_AT( Fusion_Main, "%s( %p, %p )\n", __FUNCTION__, world, ref );

     D_ASSERT( ref != NULL );

     D_MAGIC_ASSERT( world, FusionWorld );

     shared = world->shared;

     D_MAGIC_ASSERT( shared, FusionWorldShared );

     if (fusion_skirmish_prevail( &shared->fusionees_lock ))
          return;

     direct_list_foreach (fusionee, shared->fusionees) { 
          if (fusionee->id == world->fusion_id)
               continue;
          
          direct_list_foreach (fusionee_ref, fusionee->refs) {    
               if (fusionee_ref->ref == ref) {
                    if (kill( fusionee->pid, 0 ) < 0 && errno == ESRCH) { 
                         direct_list_remove( &fusionee->refs, &fusionee_ref->link );
                         direct_list_append( &list, &fusionee_ref->link );
                    }
                    break;
               }
          }
     }

     fusion_skirmish_dismiss( &shared->fusionees_lock );

     direct_list_foreach_safe (fusionee_ref, temp, list) {
          _fusion_ref_change( ref, -fusionee_ref->count, false );
          
          SHFREE( shared->main_pool, fusionee_ref );
     }
}

void
_fusion_remove_all_locals( FusionWorld *world, const FusionRef *ref )
{
     FusionWorldShared *shared;
     __Fusionee        *fusionee;
     __FusioneeRef     *fusionee_ref, *temp;

     D_DEBUG_AT( Fusion_Main, "%s( %p, %p )\n", __FUNCTION__, world, ref );

     D_ASSERT( ref != NULL );

     D_MAGIC_ASSERT( world, FusionWorld );

     shared = world->shared;

     D_MAGIC_ASSERT( shared, FusionWorldShared );

     if (fusion_skirmish_prevail( &shared->fusionees_lock ))
          return;

     direct_list_foreach (fusionee, shared->fusionees) {
          direct_list_foreach_safe (fusionee_ref, temp, fusionee->refs) {
               if (fusionee_ref->ref == ref) {
                    direct_list_remove( &fusionee->refs, &fusionee_ref->link );
                    
                    SHFREE( shared->main_pool, fusionee_ref );
               }
          }
     }

     fusion_skirmish_dismiss( &shared->fusionees_lock );
}

static void
_fusion_remove_fusionee( FusionWorld *world, FusionID fusion_id )
{
     FusionWorldShared *shared;
     __Fusionee        *fusionee;
     __FusioneeRef     *fusionee_ref, *temp;

     D_DEBUG_AT( Fusion_Main, "%s( %p, %lu )\n", __FUNCTION__, world, fusion_id );

     D_MAGIC_ASSERT( world, FusionWorld );

     shared = world->shared;

     D_MAGIC_ASSERT( shared, FusionWorldShared );

     fusion_skirmish_prevail( &shared->fusionees_lock );

     if (fusion_id == world->fusion_id) {
          fusionee = world->fusionee;
     }
     else {
          direct_list_foreach (fusionee, shared->fusionees) {
               if (fusionee->id == fusion_id)
                    break;
          }
     }

     if (!fusionee) {
          D_DEBUG_AT( Fusion_Main, "Fusionee %lu not found!\n", fusion_id );
          fusion_skirmish_dismiss( &shared->fusionees_lock );
          return;
     }

     direct_list_remove( &shared->fusionees, &fusionee->link );

     fusion_skirmish_dismiss( &shared->fusionees_lock );
     
     direct_list_foreach_safe (fusionee_ref, temp, fusionee->refs) {
          direct_list_remove( &fusionee->refs, &fusionee_ref->link );
               
          _fusion_ref_change( fusionee_ref->ref, -fusionee_ref->count, false );
          
          SHFREE( shared->main_pool, fusionee_ref );
     }

     SHFREE( shared->main_pool, fusionee );
}

/**********************************************************************************************************************/

DirectResult
_fusion_send_message( int                 fd, 
                      const void         *msg, 
                      size_t              msg_size, 
                      struct sockaddr_un *addr )
{
     socklen_t len = sizeof(struct sockaddr_un);
     
     D_ASSERT( msg != NULL );

     if (!addr) {
          addr = alloca( sizeof(struct sockaddr_un) );
          getsockname( fd, (struct sockaddr*)addr, &len );
     }          
    
     while (sendto( fd, msg, msg_size, 0, (struct sockaddr*)addr, len ) < 0) {
          switch (errno) {
               case EINTR:
                    continue;
               case ECONNREFUSED:
                    return DR_DESTROYED;
               default:
                    break;
          }

          D_PERROR( "Fusion: sendto()\n" );

          return DR_IO;
     }
     
     return DR_OK;
}

DirectResult
_fusion_recv_message( int                 fd, 
                      void               *msg,
                      size_t              msg_size,
                      struct sockaddr_un *addr )
{
     socklen_t len = addr ? sizeof(struct sockaddr_un) : 0;
     
     D_ASSERT( msg != NULL );
    
     while (recvfrom( fd, msg, msg_size, 0, (struct sockaddr*)addr, &len ) < 0) {
          switch (errno) {
               case EINTR:
                    continue;
               case ECONNREFUSED:
                    return DR_DESTROYED;
               default:
                    break;
          }

          D_PERROR( "Fusion: recvfrom()\n" );
          
          return DR_IO;
     }
     
     return DR_OK;
}

/**********************************************************************************************************************/

static void
fusion_fork_handler_prepare( void )
{
     int i;
     
     D_DEBUG_AT( Fusion_Main, "%s()\n", __FUNCTION__ );
     
     for (i=0; i<FUSION_MAX_WORLDS; i++) {
          FusionWorld *world = fusion_worlds[i];

          if (!world)
               continue;
               
          D_MAGIC_ASSERT( world, FusionWorld );
          
          if (world->fork_callback)
               world->fork_callback( world->fork_action, FFS_PREPARE );
     }
}

static void
fusion_fork_handler_parent( void )
{
     int i;

     D_DEBUG_AT( Fusion_Main, "%s()\n", __FUNCTION__ );
    
     for (i=0; i<FUSION_MAX_WORLDS; i++) {
          FusionWorld        *world = fusion_worlds[i];
           FusionWorldShared *shared;

          if (!world)
               continue;

          D_MAGIC_ASSERT( world, FusionWorld );
          
          shared = world->shared;
          
          D_MAGIC_ASSERT( shared, FusionWorldShared );
          
          if (world->fork_callback)
               world->fork_callback( world->fork_action, FFS_PARENT );
               
          if (world->fork_action == FFA_FORK) {
               /* Increase the shared reference counter. */
               if (fusion_master( world ))
                    shared->refs++;
               
               /* Cancel the dispatcher to prevent conflicts. */
               direct_thread_cancel( world->dispatch_loop );
          }
     }
}

static void
fusion_fork_handler_child( void )
{
     int i;

     D_DEBUG_AT( Fusion_Main, "%s()\n", __FUNCTION__ );

     for (i=0; i<FUSION_MAX_WORLDS; i++) {
          FusionWorld       *world = fusion_worlds[i];
          FusionWorldShared *shared;

          if (!world)
               continue;

          D_MAGIC_ASSERT( world, FusionWorld );

          shared = world->shared;

          D_MAGIC_ASSERT( shared, FusionWorldShared );
          
          if (world->fork_callback)
               world->fork_callback( world->fork_action, FFS_CHILD );

          switch (world->fork_action) {
               default:
                    D_BUG( "unknown fork action %d", world->fork_action );

               case FFA_CLOSE:
                    D_DEBUG_AT( Fusion_Main, "  -> closing world %d\n", i );

                    /* Remove world from global list. */
                    fusion_worlds[i] = NULL;

                    /* Unmap shared area. */
                    munmap( world->shared, sizeof(FusionWorldShared) );

                    /* Close socket. */
                    close( world->fusion_fd );

                    /* Free local world data. */
                    D_MAGIC_CLEAR( world );
                    D_FREE( world );

                    break;

               case FFA_FORK: {
                    __Fusionee    *fusionee;
                    __FusioneeRef *fusionee_ref;
                    
                    D_DEBUG_AT( Fusion_Main, "  -> forking in world %d\n", i );

                    fusionee = world->fusionee;
                    
                    D_DEBUG_AT( Fusion_Main, "  -> duplicating fusion id %lu\n", world->fusion_id );
                    
                    fusion_skirmish_prevail( &shared->fusionees_lock );
                   
                    if (_fusion_add_fusionee( world, world->fusion_id )) {
                         fusion_skirmish_dismiss( &shared->fusionees_lock );
                         raise( SIGTRAP );
                    }

                    D_DEBUG_AT( Fusion_Main, "  -> duplicating local refs...\n" );
                    
                    direct_list_foreach (fusionee_ref, fusionee->refs) {
                         __FusioneeRef *new_ref;

                         new_ref = SHCALLOC( shared->main_pool, 1, sizeof(__FusioneeRef) );
                         if (!new_ref) {  
                              D_OOSHM();
                              fusion_skirmish_dismiss( &shared->fusionees_lock );
                              raise( SIGTRAP );
                         }

                         new_ref->ref   = fusionee_ref->ref;
                         new_ref->count = fusionee_ref->count;
                         /* Avoid locking. */ 
                         new_ref->ref->multi.builtin.local += new_ref->count;

                         direct_list_append( &((__Fusionee*)world->fusionee)->refs, &new_ref->link );
                    }

                    fusion_skirmish_dismiss( &shared->fusionees_lock );

                    D_DEBUG_AT( Fusion_Main, "  -> restarting dispatcher loop...\n" );
                    
                    /* Restart the dispatcher thread. FIXME: free old struct */
                    world->dispatch_loop = direct_thread_create( DTT_MESSAGING,
                                                                 fusion_dispatch_loop,
                                                                 world, "Fusion Dispatch" );
                    if (!world->dispatch_loop)
                         raise( SIGTRAP );
               
               }    break;
          }
     }
}

/**********************************************************************************************************************/

/*
 * Enters a fusion world by joining or creating it.
 *
 * If <b>world</b> is negative, the next free index is used to create a new world.
 * Otherwise the world with the specified index is joined or created.
 */
DirectResult
fusion_enter( int               world_index,
              int               abi_version,
              FusionEnterRole   role,
              FusionWorld     **ret_world )
{
     DirectResult        ret     = DR_OK;
     int                 fd      = -1;
     FusionID            id      = -1;
     FusionWorld        *world   = NULL;
     FusionWorldShared  *shared  = MAP_FAILED;
     struct sockaddr_un  addr;
     char                buf[128];
     int                 len, err;
     int                 orig_index = world_index;

     D_DEBUG_AT( Fusion_Main, "%s( %d, %d, %p )\n", __FUNCTION__, world_index, abi_version, ret_world );

     D_ASSERT( ret_world != NULL );

     if (world_index >= FUSION_MAX_WORLDS) {
          D_ERROR( "Fusion/Init: World index %d exceeds maximum index %d!\n", world_index, FUSION_MAX_WORLDS - 1 );
          return DR_INVARG;
     }
     
     if (fusion_config->force_slave)
          role = FER_SLAVE;

     pthread_once( &fusion_init_once, init_once );

     direct_initialize();

     pthread_mutex_lock( &fusion_worlds_lock );

retry:
     world_index = orig_index;
     fd      = -1;
     id      = -1;
     world   = NULL;
     shared  = MAP_FAILED;

     fd = socket( PF_LOCAL, SOCK_RAW, 0 );
     if (fd < 0) {
          D_PERROR( "Fusion/Init: Error creating local socket!\n" );
          pthread_mutex_unlock( &fusion_worlds_lock );
          return DR_IO;
     }
          
     /* Set close-on-exec flag. */
     if (fcntl( fd, F_SETFD, FD_CLOEXEC ) < 0)
          D_PERROR( "Fusion/Init: Couldn't set close-on-exec flag!\n" );

     addr.sun_family = AF_UNIX;
     
     if (world_index < 0) {
          if (role == FER_SLAVE) {
               D_ERROR( "Fusion/Init: Slave role and a new world (index -1) was requested!\n" );
               pthread_mutex_unlock( &fusion_worlds_lock );
               close( fd );
               return DR_INVARG;
          }
          
          for (world_index=0; world_index<FUSION_MAX_WORLDS; world_index++) {
               if (fusion_worlds[world_index])
                    continue;

               len = snprintf( addr.sun_path, sizeof(addr.sun_path), "/tmp/.fusion-%d/", world_index );
               /* Make socket directory if it doesn't exits. */
               if (mkdir( addr.sun_path, 0775 ) == 0) {
                    chmod( addr.sun_path, 0775 );
                    if (fusion_config->shmfile_gid != (gid_t)-1)
                         chown( addr.sun_path, -1, fusion_config->shmfile_gid );
               }
               
               snprintf( addr.sun_path+len, sizeof(addr.sun_path)-len, "%lx", FUSION_ID_MASTER );
               
               /* Bind to address. */
               err = bind( fd, (struct sockaddr*)&addr, sizeof(addr) );
               if (err == 0) {
                    chmod( addr.sun_path, 0660 );
                    /* Change group, if requested. */
                    if (fusion_config->shmfile_gid != (gid_t)-1)
                         chown( addr.sun_path, -1, fusion_config->shmfile_gid );
                    id = FUSION_ID_MASTER;
                    break;
               }
          }            
     }
     else {
          world = fusion_worlds[world_index];
          if (!world) {
               len = snprintf( addr.sun_path, sizeof(addr.sun_path), "/tmp/.fusion-%d/", world_index );
               /* Make socket directory if it doesn't exits. */
               if (mkdir( addr.sun_path, 0775 ) == 0) {
                    chmod( addr.sun_path, 0775 );
                    if (fusion_config->shmfile_gid != (gid_t)-1)
                         chown( addr.sun_path, -1, fusion_config->shmfile_gid );
               }
               
               /* Check wether we are master. */
               snprintf( addr.sun_path+len, sizeof(addr.sun_path)-len, "%lx", FUSION_ID_MASTER );
               
               err = bind( fd, (struct sockaddr*)&addr, sizeof(addr) );
               if (err < 0) {
                    if (role == FER_MASTER) {
                         D_ERROR( "Fusion/Main: Couldn't start session as master! Remove %s.\n", addr.sun_path );
                         ret = DR_INIT;
                         goto error;
                    }
                    
                    /* Auto generate slave id. */
                    for (id = FUSION_ID_MASTER+1; id < (FusionID)-1; id++) {
                         snprintf( addr.sun_path+len, sizeof(addr.sun_path)-len, "%lx", id );
                         err = bind( fd, (struct sockaddr*)&addr, sizeof(addr) );
                         if (err == 0) {
                              chmod( addr.sun_path, 0660 );
                               /* Change group, if requested. */
                              if (fusion_config->shmfile_gid != (gid_t)-1)
                                   chown( addr.sun_path, -1, fusion_config->shmfile_gid );
                              break;
                         }
                    }
               }
               else if (err == 0 && role != FER_SLAVE) { 
                    chmod( addr.sun_path, 0660 );
                    /* Change group, if requested. */
                    if (fusion_config->shmfile_gid != (gid_t)-1)
                         chown( addr.sun_path, -1, fusion_config->shmfile_gid ); 
                    id = FUSION_ID_MASTER;
               }
          }
     }
     
     /* Enter a world again? */
     if (world) {
          D_MAGIC_ASSERT( world, FusionWorld );
          D_ASSERT( world->refs > 0 );

          /* Check the role again. */
          switch (role) {
               case FER_MASTER:
                    if (world->fusion_id != FUSION_ID_MASTER) {
                         D_ERROR( "Fusion/Init: Master role requested for a world (%d) "
                                  "we're already slave in!\n", world_index );
                         ret = DR_UNSUPPORTED;
                         goto error;
                    }
                    break;

               case FER_SLAVE:
                    if (world->fusion_id == FUSION_ID_MASTER) {
                         D_ERROR( "Fusion/Init: Slave role requested for a world (%d) "
                                  "we're already master in!\n", world_index );
                         ret = DR_UNSUPPORTED;
                         goto error;
                    }
                    break;

               case FER_ANY:
                    break;
          }

          shared = world->shared;

          D_MAGIC_ASSERT( shared, FusionWorldShared );

          if (shared->world_abi != abi_version) {
               D_ERROR( "Fusion/Init: World ABI (%d) of world '%d' doesn't match own (%d)!\n",
                        shared->world_abi, world_index, abi_version );
               ret = DR_VERSIONMISMATCH;
               goto error;
          }

          world->refs++;

          pthread_mutex_unlock( &fusion_worlds_lock );

          D_DEBUG_AT( Fusion_Main, "  -> using existing world %p [%d]\n", world, world_index );

          close( fd );

          /* Return the world. */
          *ret_world = world;

          return DR_OK;
     }
     
     if (id == (FusionID)-1) {
          D_ERROR( "Fusion/Init: Opening fusion socket (world %d) as '%s' failed!\n", world_index,
                    role == FER_ANY ? "any" : (role == FER_MASTER ? "master" : "slave")  );
          ret = DR_INIT;
          goto error;
     }
     
     D_DEBUG_AT( Fusion_Main, "  -> Fusion ID 0x%08lx\n", id );

     if (id == FUSION_ID_MASTER) {
          int shared_fd;
          
          snprintf( buf, sizeof(buf), "%s/fusion.%d.core", 
                    fusion_config->tmpfs ? : "/dev/shm", world_index );
           
          /* Open shared memory file. */         
          shared_fd = open( buf, O_RDWR | O_CREAT | O_TRUNC, 0660 );
          if (shared_fd < 0) {
               D_PERROR( "Fusion/Init: Couldn't open shared memory file!\n" );
               ret = DR_INIT;
               goto error;
          }

          if (fusion_config->shmfile_gid != (gid_t)-1) {
               if (fchown( shared_fd, -1, fusion_config->shmfile_gid ) != 0)
                    D_INFO( "Fusion/Init: Changing owner on %s failed... continuing on.\n", buf );
          }
         
          fchmod( shared_fd, 0660 );
          ftruncate( shared_fd, sizeof(FusionWorldShared) );
          
          unsigned long  size = direct_page_align(sizeof(FusionWorldShared));
          unsigned long  base = (unsigned long) 0x20000000 + (size + direct_pagesize()) * world_index;

          /* Map shared area. */
          shared = mmap( (void*) base, size,
                         PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, shared_fd, 0 );
          if (shared == MAP_FAILED) {
               D_PERROR( "Fusion/Init: Mapping shared area failed!\n" );
               close( shared_fd );
               ret = DR_INIT;
               goto error;
          }
          
          close( shared_fd );
          
          D_DEBUG_AT( Fusion_Main, "  -> shared area at %p, size %zu\n", shared, sizeof(FusionWorldShared) );
          
          /* Initialize reference counter. */
          shared->refs = 1;
          
          /* Set ABI version. */
          shared->world_abi = abi_version;

          /* Set the world index. */
          shared->world_index = world_index;

          /* Set pool allocation base/max. */
          shared->pool_base = (void*)0x20000000 + (size + direct_pagesize()) * FUSION_MAX_WORLDS + 0x8000000 * world_index;
          shared->pool_max  = shared->pool_base + 0x8000000 - 1;

          /* Set start time of world clock. */
          shared->start_time = direct_clock_get_time( DIRECT_CLOCK_SESSION );

          D_MAGIC_SET( shared, FusionWorldShared );
     }
     else {
          FusionEnter enter;
          int         shared_fd;
          
          /* Fill enter information. */
          enter.type      = FMT_ENTER;
          enter.fusion_id = id;
          
          snprintf( addr.sun_path, sizeof(addr.sun_path),
                    "/tmp/.fusion-%d/%lx", world_index, FUSION_ID_MASTER );
          
          /* Send enter message (used to sync with the master) */
          ret = _fusion_send_message( fd, &enter, sizeof(FusionEnter), &addr );
          if (ret == DR_DESTROYED) {
               struct dirent *entry = NULL;
               struct dirent  tmp;
               DIR           *dir;

               D_DEBUG_AT( Fusion_Main, "  -> master seems dead, cleaning up...\n" );

               snprintf( addr.sun_path, sizeof(addr.sun_path),
                         "/tmp/.fusion-%d", world_index );

               dir = opendir( addr.sun_path );
               if (!dir) {
                    D_PERROR( "Fusion/Enter: ERROR opening directory '%s' for cleanup!\n", addr.sun_path );

                    /* Unbind. */
                    socklen_t len = sizeof(addr);
                    if (getsockname( fd, (struct sockaddr*)&addr, &len ) == 0)
                         unlink( addr.sun_path );

                    close( fd );

                    ret = DR_INIT;
                    goto error;
               }

               while (readdir_r( dir, &tmp, &entry ) == 0 && entry) {
                    if (!strcmp( entry->d_name, "." ) || !strcmp( entry->d_name, ".." ))
                         continue;

                    snprintf( addr.sun_path, sizeof(addr.sun_path),
                              "/tmp/.fusion-%d/%s", world_index, entry->d_name );

                    D_DEBUG_AT( Fusion_Main, "  -> removing '%s'\n", addr.sun_path );

                    if (unlink( addr.sun_path ) < 0) {
                         D_PERROR( "Fusion/Enter: ERROR deleting '%s' for cleanup!\n", addr.sun_path );
                         closedir( dir );

                         /* Unbind. */
                         socklen_t len = sizeof(addr);
                         if (getsockname( fd, (struct sockaddr*)&addr, &len ) == 0)
                              unlink( addr.sun_path );

                         close( fd );

                         ret = DR_ACCESSDENIED;
                         goto error;
                    }
               }

               closedir( dir );

               /* Unbind. */
               socklen_t len = sizeof(addr);
               if (getsockname( fd, (struct sockaddr*)&addr, &len ) == 0)
                    unlink( addr.sun_path );

               close( fd );

               D_DEBUG_AT( Fusion_Main, "  -> retrying...\n" );
               goto retry;
          }
          if (ret)
               D_DERROR( ret, "Fusion/Enter: Send message failed!\n" );
          if (ret == DR_OK) {
               ret = _fusion_recv_message( fd, &enter, sizeof(FusionEnter), NULL );
               if (ret)
                    D_DERROR( ret, "Fusion/Enter: Receive message failed!\n" );
               if (ret == DR_OK && enter.type != FMT_ENTER) {
                    D_ERROR( "Fusion/Init: Expected message ENTER, got '%d'!\n", enter.type );
                    ret = DR_FUSION;
               }
          }
               
          if (ret) {
               D_ERROR( "Fusion/Init: Could not enter world '%d'!\n", world_index );
               goto error;
          }
          
          snprintf( buf, sizeof(buf), "%s/fusion.%d.core", 
                    fusion_config->tmpfs ? : "/dev/shm", world_index );
          
          /* Open shared memory file. */
          shared_fd = open( buf, O_RDWR );
          if (shared_fd < 0) {
               D_PERROR( "Fusion/Init: Couldn't open shared memory file!\n" );
               ret = DR_INIT;
               goto error;
          }
          
          unsigned long  size = direct_page_align(sizeof(FusionWorldShared));
          unsigned long  base = (unsigned long) 0x20000000 + (size + direct_pagesize()) * world_index;

          /* Map shared area. */
          shared = mmap( (void*) base, size,
                         PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, shared_fd, 0 );
          if (shared == MAP_FAILED) {
               D_PERROR( "Fusion/Init: Mapping shared area failed!\n" );
               close( shared_fd );
               ret = DR_INIT;
               goto error;
          }
          
          close( shared_fd );
          
          D_DEBUG_AT( Fusion_Main, "  -> shared area at %p, size %zu\n", shared, sizeof(FusionWorldShared) );
          
          D_MAGIC_ASSERT( shared, FusionWorldShared );

          /* Check ABI version. */
          if (shared->world_abi != abi_version) {
               D_ERROR( "Fusion/Init: World ABI (%d) doesn't match own (%d)!\n",
                        shared->world_abi, abi_version );
               ret = DR_VERSIONMISMATCH;
               goto error;
          }
     }

     /* Synchronize to world clock. */
     direct_clock_set_time( DIRECT_CLOCK_SESSION, shared->start_time );
     
     /* Allocate local data. */
     world = D_CALLOC( 1, sizeof(FusionWorld) );
     if (!world) {
          ret = D_OOM();
          goto error;
     }

     /* Initialize local data. */
     world->refs      = 1;
     world->shared    = shared;
     world->fusion_fd = fd;
     world->fusion_id = id;

     D_MAGIC_SET( world, FusionWorld );

     fusion_worlds[world_index] = world;

     /* Initialize shared memory part. */
     ret = fusion_shm_init( world );
     if (ret)
          goto error2;

     D_DEBUG_AT( Fusion_Main, "  -> initializing other parts...\n" );

     direct_mutex_init( &world->refs_lock );

     /* Initialize other parts. */
     if (world->fusion_id == FUSION_ID_MASTER) {
          fusion_skirmish_init( &shared->arenas_lock, "Fusion Arenas", world );
          fusion_skirmish_init( &shared->reactor_globals, "Fusion Reactor Globals", world );
          fusion_skirmish_init( &shared->fusionees_lock, "Fusionees", world );

          /* Create the main pool. */
          ret = fusion_shm_pool_create( world, "Fusion Main Pool", 0x100000,
                                        fusion_config->debugshm, &shared->main_pool );
          if (ret)
               goto error3;

          fusion_hash_create( shared->main_pool, HASH_INT, HASH_PTR, 109, &shared->call_hash );

          fusion_call_init( &shared->refs_call, world_refs_call, world, world );
          fusion_call_set_name( &shared->refs_call, "world_refs" );
          fusion_call_add_permissions( &shared->refs_call, 0, FUSION_CALL_PERMIT_EXECUTE );

          direct_map_create( 37, refs_map_compare, refs_map_hash, world, &world->refs_map );
     }
     else {
          direct_map_create( 37, refs_map_slave_compare, refs_map_slave_hash, world, &world->refs_map );

//          fusion_hash_create( shared->main_pool, HASH_INT, HASH_PTR, 109, &shared->call_hash );
     }

     /* Add ourselves to the list of fusionees. */
     ret = _fusion_add_fusionee( world, id );
     if (ret)
          goto error4;
          
     D_DEBUG_AT( Fusion_Main, "  -> starting dispatcher loop...\n" );

     /* Start the dispatcher thread. */
     world->dispatch_loop = direct_thread_create( DTT_MESSAGING,
                                                  fusion_dispatch_loop,
                                                  world, "Fusion Dispatch" );
     if (!world->dispatch_loop) {
          ret = DR_FAILURE;
          goto error5;
     }

     D_DEBUG_AT( Fusion_Main, "  -> done. (%p)\n", world );

     pthread_mutex_unlock( &fusion_worlds_lock );

     /* Return the fusion world. */
     *ret_world = world;

     return DR_OK;


error5:
     if (world->dispatch_loop)
          direct_thread_destroy( world->dispatch_loop );
     
     _fusion_remove_fusionee( world, id );
     
error4:
     if (world->fusion_id == FUSION_ID_MASTER)
          fusion_shm_pool_destroy( world, shared->main_pool );

error3:
     if (world->fusion_id == FUSION_ID_MASTER) {
          fusion_skirmish_destroy( &shared->arenas_lock );
          fusion_skirmish_destroy( &shared->reactor_globals );
          fusion_skirmish_destroy( &shared->fusionees_lock );
     }

     fusion_shm_deinit( world );


error2:
     fusion_worlds[world_index] = world;

     D_MAGIC_CLEAR( world );

     D_FREE( world );

error:
     if (shared != MAP_FAILED) {
          if (id == FUSION_ID_MASTER)
               D_MAGIC_CLEAR( shared );

          munmap( shared, sizeof(FusionWorldShared) );
     }

     if (fd != -1) {
          /* Unbind. */
          socklen_t len = sizeof(addr);
          if (getsockname( fd, (struct sockaddr*)&addr, &len ) == 0)
               unlink( addr.sun_path );
               
          close( fd );
     }

     pthread_mutex_unlock( &fusion_worlds_lock );

     direct_shutdown();

     return ret;
}

DirectResult
fusion_stop_dispatcher( FusionWorld *world,
                        bool         emergency )
{
     if (!world->dispatch_loop)
          return DR_OK;

     if (!emergency) {
          fusion_sync( world );

          direct_thread_lock( world->dispatch_loop );
     }

     world->dispatch_stop = true;

     if (!emergency) {
          direct_thread_unlock( world->dispatch_loop );

          fusion_sync( world );
     }

     return DR_OK;
}

/*
 * Exits the fusion world.
 *
 * If 'emergency' is true the function won't join but kill the dispatcher thread.
 */
DirectResult
fusion_exit( FusionWorld *world,
             bool         emergency )
{
     FusionWorldShared *shared;
     int                world_index;
     bool               clear = false;

     D_DEBUG_AT( Fusion_Main, "%s( %p, %semergency )\n", __FUNCTION__, world, emergency ? "" : "no " );

     D_MAGIC_ASSERT( world, FusionWorld );

     shared = world->shared;

     D_MAGIC_ASSERT( shared, FusionWorldShared );
     
     world_index = shared->world_index;

     pthread_mutex_lock( &fusion_worlds_lock );

     D_ASSERT( world->refs > 0 );

     if (--world->refs) {
          pthread_mutex_unlock( &fusion_worlds_lock );
          return DR_OK;
     }
 
     if (!emergency) {
          FusionMessageType msg = FMT_SEND;

          /* Wakeup dispatcher. */
          if (_fusion_send_message( world->fusion_fd, &msg, sizeof(msg), NULL ))
               direct_thread_cancel( world->dispatch_loop );

          /* Wait for its termination. */
          direct_thread_join( world->dispatch_loop );
     }

     direct_thread_destroy( world->dispatch_loop );

     /* Remove ourselves from list. */
     if (!emergency || fusion_master( world )) {
          _fusion_remove_fusionee( world, world->fusion_id );
     }
     else {
          struct sockaddr_un addr;
          FusionLeave        leave;

          addr.sun_family = AF_UNIX;
          snprintf( addr.sun_path, sizeof(addr.sun_path), 
                    "/tmp/.fusion-%d/%lx", world_index, FUSION_ID_MASTER );

          leave.type      = FMT_LEAVE;
          leave.fusion_id = world->fusion_id;

          _fusion_send_message( world->fusion_fd, &leave, sizeof(FusionLeave), &addr );
     }

     direct_mutex_deinit( &world->refs_lock );
     direct_map_destroy( world->refs_map );

     /* Master has to deinitialize shared data. */
     if (fusion_master( world )) {
          fusion_call_destroy( &shared->refs_call );

          fusion_hash_destroy( shared->call_hash );

          shared->refs--;
          if (shared->refs == 0) {
               fusion_skirmish_destroy( &shared->reactor_globals );
               fusion_skirmish_destroy( &shared->arenas_lock );
               fusion_skirmish_destroy( &shared->fusionees_lock );

               fusion_shm_pool_destroy( world, shared->main_pool );
          
               /* Deinitialize shared memory. */
               fusion_shm_deinit( world );
          
               clear = true;
          }
     }
     else {
          /* Leave shared memory. */
          fusion_shm_deinit( world );
     }

     /* Reset local dispatch nodes. */
     _fusion_reactor_free_all( world );

     /* Remove world from global list. */
     fusion_worlds[shared->world_index] = NULL;

     /* Unmap shared area. */
     if (clear)
          D_MAGIC_CLEAR( shared );
     
     munmap( shared, sizeof(FusionWorldShared) );
     
     /* Close socket. */     
     close( world->fusion_fd );

     if (clear) {
          DIR  *dir;
          char  buf[128];
          int   len;
          
          /* Remove core shmfile. */
          snprintf( buf, sizeof(buf), "%s/fusion.%d.core", 
                    fusion_config->tmpfs ? : "/dev/shm", world_index );
          D_DEBUG_AT( Fusion_Main, "Removing shmfile %s.\n", buf );
          unlink( buf );
          
          /* Cleanup socket directory. */
          len = snprintf( buf, sizeof(buf), "/tmp/.fusion-%d/", world_index );
          dir = opendir( buf );
          if (dir) {
               struct dirent *entry = NULL;
               struct dirent  tmp;
               
               while (readdir_r( dir, &tmp, &entry ) == 0 && entry) {
                    if (entry->d_name[0] != '.') {
                         struct stat st;
                         
                         direct_snputs( buf+len, entry->d_name, sizeof(buf)-len );
                         if (stat( buf, &st ) == 0 && S_ISSOCK(st.st_mode)) {
                              D_DEBUG_AT( Fusion_Main, "Removing socket %s.\n", buf );
                              unlink( buf );
                         }
                    }
               }
               
               closedir( dir );
          }
          else {
               D_PERROR( "Fusion/Main: Couldn't open socket directory %s", buf );
          }
     }
     
     /* Free local world data. */
     D_MAGIC_CLEAR( world );
     D_FREE( world );

     D_DEBUG_AT( Fusion_Main, "%s( %p ) done.\n", __FUNCTION__, world );

     pthread_mutex_unlock( &fusion_worlds_lock );

     direct_shutdown();

     return DR_OK;
}

/*
 * Sends a signal to one or more fusionees and optionally waits
 * for their processes to terminate.
 *
 * A fusion_id of zero means all fusionees but the calling one.
 * A timeout of zero means infinite waiting while a negative value
 * means no waiting at all.
 */
DirectResult
fusion_kill( FusionWorld *world,
             FusionID     fusion_id,
             int          signal,
             int          timeout_ms )
{
     FusionWorldShared *shared;
     __Fusionee        *fusionee, *temp;
     int                result;

     D_DEBUG_AT( Fusion_Main, "%s( %p, %lu, %d, %d )\n", 
                 __FUNCTION__, world, fusion_id, signal, timeout_ms );
     
     D_MAGIC_ASSERT( world, FusionWorld );
     
     shared = world->shared;

     D_MAGIC_ASSERT( shared, FusionWorldShared );
     
     fusion_skirmish_prevail( &shared->fusionees_lock );
     
     direct_list_foreach_safe (fusionee, temp, shared->fusionees) {          
          if (fusion_id == 0 && fusionee->id == world->fusion_id)
               continue;
                
          if (fusion_id != 0 && fusionee->id != fusion_id)
               continue;

          D_DEBUG_AT( Fusion_Main, " -> killing fusionee %lu (%d)...\n", fusionee->id, fusionee->pid );
          
          result = kill( fusionee->pid, signal );
          if (result == 0 && timeout_ms >= 0) {
               pid_t     pid  = fusionee->pid;
               long long stop = timeout_ms ? (direct_clock_get_micros() + timeout_ms*1000) : 0;
               
               fusion_skirmish_dismiss( &shared->fusionees_lock );
          
               while (kill( pid, 0 ) == 0) { 
                    usleep( 1000 );
                    
                    if (timeout_ms && direct_clock_get_micros() >= stop)
                         break;
               };
               
               fusion_skirmish_prevail( &shared->fusionees_lock );
          }
          else if (result < 0) {
               if (errno == ESRCH) {
                    D_DEBUG_AT( Fusion_Main, " ... fusionee %lu exited without removing itself!\n", fusionee->id );

                    _fusion_remove_fusionee( world, fusionee->id );
               }
               else {
                    D_PERROR( "Fusion/Main: kill(%d, %d)\n", fusionee->pid, signal );
               }
          } 
     }
     
     fusion_skirmish_dismiss( &shared->fusionees_lock );

     return DR_OK;
}

/**********************************************************************************************************************/

DirectResult
fusion_world_activate( FusionWorld *world )
{
     return DR_OK;
}

DirectResult
fusion_dispatch_cleanup_add( FusionWorld                  *world,
                             FusionDispatchCleanupFunc     func,
                             void                         *ctx,
                             FusionDispatchCleanup       **ret_cleanup )
{
     FusionDispatchCleanup *cleanup;

     cleanup = D_CALLOC( 1, sizeof(FusionDispatchCleanup) );
     if (!cleanup)
          return D_OOM();

     cleanup->func = func;
     cleanup->ctx  = ctx;

     direct_list_append( &world->dispatch_cleanups, &cleanup->link );

     *ret_cleanup = cleanup;

     return DR_OK;
}

DirectResult
fusion_dispatch_cleanup_remove( FusionWorld                  *world,
                                FusionDispatchCleanup        *cleanup )
{
     direct_list_remove( &world->dispatch_cleanups, &cleanup->link );

     D_FREE( cleanup );

     return DR_OK;
}

static void
handle_dispatch_cleanups( FusionWorld *world )
{
     FusionDispatchCleanup *cleanup, *next;

     D_DEBUG_AT( Fusion_Main_Dispatch, "%s( %p )\n", __FUNCTION__, world );

     direct_list_foreach_safe (cleanup, next, world->dispatch_cleanups) {
#if D_DEBUG_ENABLED
          if (direct_log_domain_check( &Fusion_Main_Dispatch )) // avoid call to direct_trace_lookup_symbol_at
               D_DEBUG_AT( Fusion_Main_Dispatch, "  -> %s (%p)\n", direct_trace_lookup_symbol_at( cleanup->func ), cleanup->ctx );
#endif

          cleanup->func( cleanup->ctx );

          D_FREE( cleanup );
     }

     D_DEBUG_AT( Fusion_Main_Dispatch, "  -> cleanups done.\n" );

     world->dispatch_cleanups = NULL;
}

static DirectEnumerationResult
refs_iterate( DirectMap    *map,
              void         *object,
              void         *ctx )
{
     FusionRefSlaveEntry *entry = object;

     if (entry->key.fusion_id == *((FusionID*)ctx)) {
          int i;

          for (i=0; i<entry->refs; i++)
               fusion_ref_down( entry->ref, false );

          D_FREE( entry );

          return DENUM_REMOVE;
     }

     return DENUM_OK;
}

static void *
fusion_dispatch_loop( DirectThread *self, void *arg )
{
     FusionWorld        *world = arg;
     struct sockaddr_un  addr;
     socklen_t           addr_len = sizeof(addr); 
     fd_set              set;
     char                buf[FUSION_MESSAGE_SIZE];

     D_DEBUG_AT( Fusion_Main_Dispatch, "%s() running...\n", __FUNCTION__ );

     while (true) {
          int     result;
          ssize_t msg_size;
          
          D_MAGIC_ASSERT( world, FusionWorld );

          FD_ZERO( &set );
          FD_SET( world->fusion_fd, &set );

          result = select( world->fusion_fd + 1, &set, NULL, NULL, NULL );
          if (result < 0) {
               switch (errno) {
                    case EINTR:
                         continue;

                    default:
                         D_PERROR( "Fusion/Dispatcher: select() failed!\n" );
                         return NULL;
               }
          }

          D_MAGIC_ASSERT( world, FusionWorld );

          if (FD_ISSET( world->fusion_fd, &set ) && 
              (msg_size = recvfrom( world->fusion_fd, buf, sizeof(buf), 0, (struct sockaddr*)&addr, &addr_len )) > 0) {
               FusionMessage *msg = (FusionMessage*)buf;               

               pthread_setcancelstate( PTHREAD_CANCEL_DISABLE, NULL );

               D_DEBUG_AT( Fusion_Main_Dispatch, " -> message from '%s'...\n", addr.sun_path );

               direct_thread_lock( self );

               if (world->dispatch_stop) {
                    D_DEBUG_AT( Fusion_Main_Dispatch, "  -> IGNORING (dispatch_stop!)\n" );
               }
               else {
                    switch (msg->type) {
                         case FMT_SEND:
                              D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_SEND...\n" );
                              break;

                         case FMT_ENTER:
                              D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_ENTER...\n" ); 
                              if (!fusion_master( world )) {
                                   D_ERROR( "Fusion/Dispatch: Got ENTER request, but I'm not master!\n" );
                                   break;
                              }
                              if (msg->enter.fusion_id == world->fusion_id) {
                                   D_ERROR( "Fusion/Dispatch: Received ENTER request from myself!\n" );
                                   break;
                              }
                              /* Nothing to do here. Send back message. */
                              _fusion_send_message( world->fusion_fd, msg, sizeof(FusionEnter), &addr );
                              break;

                         case FMT_LEAVE:
                              D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_LEAVE...\n" );
                              if (!fusion_master( world )) {
                                   D_ERROR( "Fusion/Dispatch: Got LEAVE request, but I'm not master!\n" );
                                   break;
                              }
                              if (world->fusion_id == FUSION_ID_MASTER) {
                                   direct_mutex_lock( &world->refs_lock );
                                   direct_map_iterate( world->refs_map, refs_iterate, &msg->leave.fusion_id );
                                   direct_mutex_unlock( &world->refs_lock );
                              }
                              if (msg->leave.fusion_id == world->fusion_id) {
                                   D_ERROR( "Fusion/Dispatch: Received LEAVE request from myself!\n" );
                                   break;
                              }
                              _fusion_remove_fusionee( world, msg->leave.fusion_id );
                              break;

                         case FMT_CALL:
                              D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_CALL...\n" );

                              if (((FusionCallMessage*)msg)->caller == 0)    // FIXME: currently caller is set to non-zero even for ref_watch
                                   handle_dispatch_cleanups( world );

                              _fusion_call_process( world, msg->call.call_id, &msg->call,
                                                    (msg_size != sizeof(FusionCallMessage)) ? (((FusionCallMessage*)msg) + 1) : NULL );
                              break;

                         case FMT_REACTOR:
                              D_DEBUG_AT( Fusion_Main_Dispatch, "  -> FMT_REACTOR...\n" );
                              _fusion_reactor_process_message( world, msg->reactor.id, msg->reactor.channel, 
                                                               &buf[sizeof(FusionReactorMessage)] );
                              if (msg->reactor.ref) {
                                   fusion_ref_down( msg->reactor.ref, true );
                                   if (fusion_ref_zero_trylock( msg->reactor.ref ) == DR_OK) {
                                        fusion_ref_destroy( msg->reactor.ref );
                                        SHFREE( world->shared->main_pool, msg->reactor.ref );
                                   }
                              }
                              break;                    

                         default:
                              D_BUG( "unexpected message type (%d)", msg->type );
                              break;
                    }
               }

               handle_dispatch_cleanups( world );

               direct_thread_unlock( self );

               if (!world->refs) {
                    D_DEBUG_AT( Fusion_Main_Dispatch, "  -> good bye!\n" );
                    return NULL;
               }

               D_DEBUG_AT( Fusion_Main_Dispatch, " ...done\n" );

               pthread_setcancelstate( PTHREAD_CANCEL_ENABLE, NULL );
          }
     }

     return NULL;
}

/*
 * Wait until all pending messages are processed.
 */
DirectResult
fusion_sync( const FusionWorld *world )
{
     D_MAGIC_ASSERT( world, FusionWorld );

     D_DEBUG_AT( Fusion_Main, "%s( %p )\n", __FUNCTION__, world );

     D_DEBUG_AT( Fusion_Main, "  -> syncing with fusion device...\n" );

     D_UNIMPLEMENTED();

     D_DEBUG_AT( Fusion_Main, "  -> synced\n");

     return DR_OK;
}

const char *
fusion_get_tmpfs( FusionWorld *world )
{
     D_MAGIC_ASSERT( world, FusionWorld );
     D_MAGIC_ASSERT( world->shared, FusionWorldShared );

     return "/tmp";
}

DirectResult
fusion_get_fusionee_path( const FusionWorld *world,
                          FusionID           fusion_id,
                          char              *buf,
                          size_t             buf_size,
                          size_t            *ret_size )
{
     D_UNIMPLEMENTED();

     return DR_UNIMPLEMENTED;
}

DirectResult
fusion_get_fusionee_pid( const FusionWorld *world,
                         FusionID           fusion_id,
                         pid_t             *ret_pid )
{
     D_UNIMPLEMENTED();

     return DR_UNIMPLEMENTED;
}

DirectResult
fusion_world_set_root( FusionWorld *world,
                       void        *root )
{
     D_ASSERT( world != NULL );
     D_ASSERT( world->shared != NULL );

     if (world->fusion_id != FUSION_ID_MASTER)
          return DR_ACCESSDENIED;

     world->shared->world_root = root;

     return DR_OK;
}

void *
fusion_world_get_root( FusionWorld *world )
{
     D_ASSERT( world != NULL );
     D_ASSERT( world->shared != NULL );

     return world->shared->world_root;
}

/**********************************************************************************************************************/

#endif /* FUSION_BUILD_KERNEL */

/*
 * Sets the fork() action of the calling Fusionee within the world.
 */
void
fusion_world_set_fork_action( FusionWorld      *world,
                              FusionForkAction  action )
{
     D_MAGIC_ASSERT( world, FusionWorld );

     world->fork_action = action;
}

/*
 * Gets the current fork() action.
 */
FusionForkAction 
fusion_world_get_fork_action( FusionWorld *world )
{
     D_MAGIC_ASSERT( world, FusionWorld );
     
     return world->fork_action;
}

/*
 * Registers a callback called upon fork().
 */
void
fusion_world_set_fork_callback( FusionWorld        *world,
                                FusionForkCallback  callback )
{
     D_MAGIC_ASSERT( world, FusionWorld );
     
     world->fork_callback = callback;
}

/*
 * Registers a callback called when a slave exits.
 */
void
fusion_world_set_leave_callback( FusionWorld         *world,
                                 FusionLeaveCallback  callback,
                                 void                *ctx )
{
     D_MAGIC_ASSERT( world, FusionWorld );

     world->leave_callback = callback;
     world->leave_ctx      = ctx;
}

/*
 * Return the index of the specified world.
 */
int
fusion_world_index( const FusionWorld *world )
{
     FusionWorldShared *shared;

     D_MAGIC_ASSERT( world, FusionWorld );

     shared = world->shared;

     D_MAGIC_ASSERT( shared, FusionWorldShared );

     return shared->world_index;
}

/*
 * Return the own Fusion ID within the specified world.
 */
FusionID
fusion_id( const FusionWorld *world )
{
     D_MAGIC_ASSERT( world, FusionWorld );

     return world->fusion_id;
}

/*
 * Return if the world is a multi application world.
 */
bool
fusion_is_multi( const FusionWorld *world )
{
     D_MAGIC_ASSERT( world, FusionWorld );

     return true;
}

/*
 * Return the thread ID of the Fusion Dispatcher within the specified world.
 */
pid_t
fusion_dispatcher_tid( const FusionWorld *world )
{
     D_MAGIC_ASSERT( world, FusionWorld );

     if (world->dispatch_loop)
          return direct_thread_get_tid( world->dispatch_loop );

     return 0;
}

/*
 * Return true if this process is the master.
 */
bool
fusion_master( const FusionWorld *world )
{
     D_MAGIC_ASSERT( world, FusionWorld );

     return world->fusion_id == FUSION_ID_MASTER;
}

/*
 * Check if a pointer points to the shared memory.
 */
bool
fusion_is_shared( FusionWorld *world,
                  const void  *ptr )
{
     int              i;
     DirectResult     ret;
     FusionSHM       *shm;
     FusionSHMShared *shared;

     D_MAGIC_ASSERT( world, FusionWorld );

     shm = &world->shm;

     D_MAGIC_ASSERT( shm, FusionSHM );

     shared = shm->shared;

     D_MAGIC_ASSERT( shared, FusionSHMShared );

     if (ptr >= (void*) world->shared && ptr < (void*) world->shared + sizeof(FusionWorldShared))
          return true;

     ret = fusion_skirmish_prevail( &shared->lock );
     if (ret)
          return false;

     for (i=0; i<FUSION_SHM_MAX_POOLS; i++) {
          if (shared->pools[i].active) {
               shmalloc_heap       *heap;
               FusionSHMPoolShared *pool = &shared->pools[i];

               D_MAGIC_ASSERT( pool, FusionSHMPoolShared );

               heap = pool->heap;

               D_MAGIC_ASSERT( heap, shmalloc_heap );

               if (ptr >= pool->addr_base && ptr < pool->addr_base + heap->size) {
                    fusion_skirmish_dismiss( &shared->lock );
                    return true;
               }
          }
     }

     fusion_skirmish_dismiss( &shared->lock );

     return false;
}

#else /* FUSION_BUILD_MULTI */

static void *
event_dispatcher_loop( DirectThread *thread, void *arg )
{
     const int    call_size = sizeof( FusionEventDispatcherCall );
     FusionWorld *world     = arg;

     D_DEBUG_AT( Fusion_Main_Dispatch, "%s() running...\n", __FUNCTION__ );

     while (1) {
          FusionEventDispatcherBuffer *buf;

          direct_mutex_lock( &world->event_dispatcher_mutex );

          while (1) {
               if (!world->event_dispatcher_buffers)
                    direct_waitqueue_wait( &world->event_dispatcher_cond, &world->event_dispatcher_mutex );

               buf = (FusionEventDispatcherBuffer *)world->event_dispatcher_buffers;
               D_MAGIC_ASSERT( buf, FusionEventDispatcherBuffer );

               if (buf->can_free && buf->read_pos == buf->write_pos) {
                    // can free buffer but will do it later
                    direct_list_remove( &world->event_dispatcher_buffers, &buf->link );
                    direct_list_append( &world->event_dispatcher_buffers_remove, &buf->link );
//D_INFO("event_dispatcher_loop: remove buffer %p free %d read %d write %d sync %d pending %d\n", buf, buf->can_free, buf->read_pos, buf->write_pos, buf->sync_calls, buf->pending);
                    continue;
               }
//D_INFO("event_dispatcher_loop: 1 buffer %p free %d read %d write %d sync %d pending %d\n", buf, buf->can_free, buf->read_pos, buf->write_pos, buf->sync_calls, buf->pending);
               // need to wait for new messages in current buffer?
               if (buf->read_pos >= buf->write_pos) {
//D_INFO("waiting...\n");
                    D_ASSERT( buf->read_pos == buf->write_pos );
                    direct_waitqueue_wait( &world->event_dispatcher_cond, &world->event_dispatcher_mutex );
               }

               buf = (FusionEventDispatcherBuffer *)world->event_dispatcher_buffers;
               D_MAGIC_ASSERT( buf, FusionEventDispatcherBuffer );

               if (buf->can_free && buf->read_pos == buf->write_pos) {
                    // can free buffer but will do it later
                    direct_list_remove( &world->event_dispatcher_buffers, &buf->link );
                    direct_list_append( &world->event_dispatcher_buffers_remove, &buf->link );
//D_INFO("event_dispatcher_loop: remove buffer %p free %d read %d write %d sync %d pending %d\n", buf, buf->can_free, buf->read_pos, buf->write_pos, buf->sync_calls, buf->pending);
                    continue;
               }
//D_INFO("event_dispatcher_loop: 2 buffer %p free %d read %d write %d sync %d pending %d\n\n", buf, buf->can_free, buf->read_pos, buf->write_pos, buf->sync_calls, buf->pending);
               break;
          }

          D_MAGIC_ASSERT( buf, FusionEventDispatcherBuffer );

          FusionEventDispatcherCall *msg = (FusionEventDispatcherCall*)&buf->buffer[buf->read_pos];
//D_INFO("event_dispatcher_loop: processing buf %p free %d read %d write %d sync %d pending %d (msg %p)\n", buf, buf->can_free, buf->read_pos, buf->write_pos, buf->sync_calls, buf->pending, msg);

          D_DEBUG_AT( Fusion_Main_Dispatch, "%s() got msg %p <- arg %d, reaction %d\n", __FUNCTION__, msg, msg->call_arg, msg->reaction );

          buf->read_pos += call_size;
          if (msg->flags & FCEF_ONEWAY)
               buf->read_pos += msg->length;

          //align on 4-byte boundaries
          buf->read_pos = (buf->read_pos + 3) & ~3;

          if (world->dispatch_stop) {
               D_DEBUG_AT( Fusion_Main_Dispatch, "  -> IGNORING (dispatch_stop!)\n" );
               direct_mutex_unlock( &world->event_dispatcher_mutex );
               return NULL;
          }
          else {
               direct_mutex_unlock( &world->event_dispatcher_mutex );

               if (msg->call_handler3) {
                    if (FCHR_RETAIN == msg->call_handler3( 1, msg->call_arg, msg->ptr, msg->length, msg->call_ctx, 0, msg->ret_ptr, msg->ret_size, &msg->ret_length ))
                         D_WARN( "RETAIN!\n" );
               }
               else if (msg->call_handler) {
                    if (FCHR_RETAIN == msg->call_handler( 1, msg->call_arg, msg->ptr, msg->call_ctx, 0, &msg->ret_val ))
                         D_WARN( "RETAIN!\n" );
               }
               else if (msg->reaction == 1) {
                    FusionReactor *reactor = (FusionReactor *)msg->call_ctx;
                    Reaction      *reaction;
                    DirectLink    *link;

                    D_MAGIC_ASSERT( reactor, FusionReactor );

                    pthread_mutex_lock( &reactor->reactions_lock );

                    direct_list_foreach_safe( reaction, link, reactor->reactions ) {
                         if ((long)reaction->node_link == msg->call_arg) {
//D_INFO("dispatch reaction %p channel %d func %p\n", reaction, msg->call_arg, reaction->func);
                              if (RS_REMOVE == reaction->func( msg->ptr, reaction->ctx ))
                                   direct_list_remove( &reactor->reactions, &reaction->link );
                         }
                    }

                    pthread_mutex_unlock( &reactor->reactions_lock );
               }
               else if (msg->reaction == 2) {
                    FusionReactor *reactor = (FusionReactor *)msg->call_ctx;

                    fusion_reactor_free( reactor );
               }
               else {
                    D_ASSERT( 0 );
               }

               if (!(msg->flags & FCEF_ONEWAY)) {
                    direct_mutex_lock( &world->event_dispatcher_call_mutex );

                    msg->processed = 1;

                    direct_waitqueue_broadcast( &world->event_dispatcher_call_cond );

                    direct_mutex_unlock( &world->event_dispatcher_call_mutex );
               }

               direct_mutex_lock( &world->event_dispatcher_mutex );

               buf->pending--;
          }

          direct_waitqueue_signal( &world->event_dispatcher_process_cond );

          if (world->event_dispatcher_buffers_remove) {
               buf = (FusionEventDispatcherBuffer *)world->event_dispatcher_buffers_remove;
               D_MAGIC_ASSERT( buf, FusionEventDispatcherBuffer );

               if (!buf->sync_calls && !buf->pending) {
//D_INFO("event_dispatcher_loop: free buffer %p free %d read %d write %d sync %d pending %d\n\n", buf, buf->can_free, buf->read_pos, buf->write_pos, buf->sync_calls, buf->pending);
                    direct_list_remove( &world->event_dispatcher_buffers_remove, &buf->link );
                    D_MAGIC_CLEAR( buf );
                    D_FREE( buf );
               }
          }

          direct_mutex_unlock( &world->event_dispatcher_mutex );

          if (!world->refs) {
               D_DEBUG_AT( Fusion_Main_Dispatch, "  -> good bye!\n" );
               return NULL;
          }
     }

     D_DEBUG_AT( Fusion_Main_Dispatch, "  -> good bye!\n" );

     return NULL;
}

DirectResult
_fusion_event_dispatcher_process( FusionWorld *world, const FusionEventDispatcherCall *call, FusionEventDispatcherCall **ret )
{
     const int call_size = sizeof( FusionEventDispatcherCall );

     direct_mutex_lock( &world->event_dispatcher_mutex );

     if (world->dispatch_stop) {
          direct_mutex_unlock( &world->event_dispatcher_mutex );
          return DR_DESTROYED;
     }


     while (call->call_handler3 && (call->flags & FCEF_ONEWAY) && direct_list_count_elements_EXPENSIVE( world->event_dispatcher_buffers ) > 4) {
          direct_waitqueue_wait( &world->event_dispatcher_process_cond, &world->event_dispatcher_mutex );
     }

     if (!world->event_dispatcher_buffers) {
          FusionEventDispatcherBuffer *new_buf = D_CALLOC( 1, sizeof(FusionEventDispatcherBuffer) );
          D_MAGIC_SET( new_buf, FusionEventDispatcherBuffer );
          direct_list_append( &world->event_dispatcher_buffers, &new_buf->link );
     }

     FusionEventDispatcherBuffer *buf = (FusionEventDispatcherBuffer *)direct_list_get_last( world->event_dispatcher_buffers );
     D_MAGIC_ASSERT( buf, FusionEventDispatcherBuffer );

     if (buf->write_pos + call_size + call->length > EVENT_DISPATCHER_BUFFER_LENGTH) {
          buf->can_free = 1;
          FusionEventDispatcherBuffer *new_buf = D_CALLOC( 1, sizeof(FusionEventDispatcherBuffer) );
          D_MAGIC_SET( new_buf, FusionEventDispatcherBuffer );
          direct_list_append( &world->event_dispatcher_buffers, &new_buf->link );
          buf = new_buf;
     }

     *ret = (FusionEventDispatcherCall *)&buf->buffer[buf->write_pos];
//D_INFO("_fusion_event_dispatcher_process: buf %p free %d read %d write %d sync %d pending %d (msg %p async %d)\n", buf, buf->can_free, buf->read_pos, buf->write_pos, buf->sync_calls, buf->pending, *ret, call->flags & FCEF_ONEWAY);
     // copy data and signal dispatcher
     memcpy( *ret, call, call_size );

     buf->write_pos += call_size;

     buf->pending++;

     if (!(call->flags & FCEF_ONEWAY))
          buf->sync_calls++;

     // copy extra data to buffer
     if (call->flags & FCEF_ONEWAY && call->length) {
          (*ret)->ptr = &buf->buffer[buf->write_pos];
          memcpy( (*ret)->ptr, call->ptr, call->length );
          buf->write_pos += call->length;
     }

     // align on 4-byte boundaries
     buf->write_pos = (buf->write_pos + 3) & ~3;

     direct_waitqueue_signal( &world->event_dispatcher_cond );

     direct_mutex_unlock( &world->event_dispatcher_mutex );

     if (!(call->flags & FCEF_ONEWAY)) {
          direct_mutex_lock( &world->event_dispatcher_call_mutex );

          while (!(*ret)->processed)
               direct_waitqueue_wait( &world->event_dispatcher_call_cond, &world->event_dispatcher_call_mutex );

          direct_mutex_unlock( &world->event_dispatcher_call_mutex );

          direct_mutex_lock( &world->event_dispatcher_mutex );
          buf->sync_calls--;
          direct_mutex_unlock( &world->event_dispatcher_mutex );
     }

     return DR_OK;
}

DirectResult
_fusion_event_dispatcher_process_reactions( FusionWorld *world, FusionReactor *reactor, int channel, void *msg_data, int msg_size )
{
     const int                  call_size = sizeof( FusionEventDispatcherCall );
     FusionEventDispatcherCall  msg;
     FusionEventDispatcherCall *ret;

     msg.processed = 0;
     msg.reaction = 1;
     msg.call_handler = 0;
     msg.call_handler3 = 0;
     msg.call_ctx = reactor;
     msg.flags = FCEF_ONEWAY;
     msg.call_arg = channel;
     msg.ptr = msg_data;
     msg.length = msg_size;
     msg.ret_val = 0;
     msg.ret_ptr = 0;
     msg.ret_size = 0;
     msg.ret_length = 0;

     direct_mutex_lock( &world->event_dispatcher_mutex );

     if (world->dispatch_stop) {
          direct_mutex_unlock( &world->event_dispatcher_mutex );
          return DR_DESTROYED;
     }

     if (!world->event_dispatcher_buffers) {
          FusionEventDispatcherBuffer *new_buf = D_CALLOC( 1, sizeof(FusionEventDispatcherBuffer) );
          D_MAGIC_SET( new_buf, FusionEventDispatcherBuffer );
          direct_list_append( &world->event_dispatcher_buffers, &new_buf->link );
     }

     FusionEventDispatcherBuffer *buf = (FusionEventDispatcherBuffer *)direct_list_get_last( world->event_dispatcher_buffers );
     D_MAGIC_ASSERT( buf, FusionEventDispatcherBuffer );

     if (buf->write_pos + call_size + msg_size > EVENT_DISPATCHER_BUFFER_LENGTH) {
          buf->can_free = 1;
          FusionEventDispatcherBuffer *new_buf = D_CALLOC( 1, sizeof(FusionEventDispatcherBuffer) );
          D_MAGIC_SET( new_buf, FusionEventDispatcherBuffer );
          direct_list_append( &world->event_dispatcher_buffers, &new_buf->link );
          buf = new_buf;
     }

     ret = (FusionEventDispatcherCall *)&buf->buffer[buf->write_pos];
//D_INFO("_fusion_event_dispatcher_process_reactions: buf %p free %d read %d write %d sync %d pending %d (msg %p async %d)\n", buf, buf->can_free, buf->read_pos, buf->write_pos, buf->sync_calls, buf->pending, ret, msg.flags & FCEF_ONEWAY);
     // copy data and signal dispatcher
     memcpy( ret, &msg, call_size );

     buf->write_pos += call_size;

     buf->pending++;

     // copy extra data to buffer
     if (msg.length) {
          ret->ptr = &buf->buffer[buf->write_pos];
          memcpy( ret->ptr, msg.ptr, msg.length );
          buf->write_pos += msg.length;
     }

     // align on 4-byte boundaries
     buf->write_pos = (buf->write_pos + 3) & ~3;

     direct_waitqueue_signal( &world->event_dispatcher_cond );

     direct_mutex_unlock( &world->event_dispatcher_mutex );

     return DR_OK;
}

DirectResult
_fusion_event_dispatcher_process_reactor_free( FusionWorld *world, FusionReactor *reactor )
{
     const int                  call_size = sizeof( FusionEventDispatcherCall );
     FusionEventDispatcherCall  msg;
     FusionEventDispatcherCall *ret;

     if (reactor->free)
          return DR_OK;

     reactor->free = 1;

     msg.processed = 0;
     msg.reaction = 2;
     msg.call_handler = 0;
     msg.call_handler3 = 0;
     msg.call_ctx = reactor;
     msg.flags = FCEF_ONEWAY;
     msg.call_arg = 0;
     msg.ptr = 0;
     msg.length = 0;
     msg.ret_val = 0;
     msg.ret_ptr = 0;
     msg.ret_size = 0;
     msg.ret_length = 0;

     direct_mutex_lock( &world->event_dispatcher_mutex );

     if (world->dispatch_stop) {
          direct_mutex_unlock( &world->event_dispatcher_mutex );
          return DR_DESTROYED;
     }

     if (!world->event_dispatcher_buffers) {
          FusionEventDispatcherBuffer *new_buf = D_CALLOC( 1, sizeof(FusionEventDispatcherBuffer) );
          D_MAGIC_SET( new_buf, FusionEventDispatcherBuffer );
          direct_list_append( &world->event_dispatcher_buffers, &new_buf->link );
     }

     FusionEventDispatcherBuffer *buf = (FusionEventDispatcherBuffer *)direct_list_get_last( world->event_dispatcher_buffers );
     D_MAGIC_ASSERT( buf, FusionEventDispatcherBuffer );

     if (buf->write_pos + call_size > EVENT_DISPATCHER_BUFFER_LENGTH) {
          buf->can_free = 1;
          FusionEventDispatcherBuffer *new_buf = D_CALLOC( 1, sizeof(FusionEventDispatcherBuffer) );
          D_MAGIC_SET( new_buf, FusionEventDispatcherBuffer );
          direct_list_append( &world->event_dispatcher_buffers, &new_buf->link );
          buf = new_buf;
     }

     ret = (FusionEventDispatcherCall *)&buf->buffer[buf->write_pos];
//D_INFO("_fusion_event_dispatcher_process_reactor_free: buf %p free %d read %d write %d sync %d pending %d (msg %p async %d)\n", buf, buf->can_free, buf->read_pos, buf->write_pos, buf->sync_calls, buf->pending, ret, msg.flags & FCEF_ONEWAY);
     // copy data and signal dispatcher
     memcpy( ret, &msg, call_size );

     buf->write_pos += call_size;

     buf->pending++;

     // align on 4-byte boundaries
     buf->write_pos = (buf->write_pos + 3) & ~3;

     direct_waitqueue_signal( &world->event_dispatcher_cond );

     direct_mutex_unlock( &world->event_dispatcher_mutex );

     return DR_INCOMPLETE;
}

/*
 * Enters a fusion world by joining or creating it.
 *
 * If <b>world_index</b> is negative, the next free index is used to create a new world.
 * Otherwise the world with the specified index is joined or created.
 */
DirectResult fusion_enter( int               world_index,
                           int               abi_version,
                           FusionEnterRole   role,
                           FusionWorld     **ret_world )
{
     DirectResult  ret;
     FusionWorld  *world = NULL;

     D_ASSERT( ret_world != NULL );

     ret = direct_initialize();
     if (ret)
          return ret;

     world = D_CALLOC( 1, sizeof(FusionWorld) );
     if (!world) {
          ret = D_OOM();
          goto error;
     }

     world->shared = D_CALLOC( 1, sizeof(FusionWorldShared) );
     if (!world->shared) {
          ret = D_OOM();
          goto error;
     }

     world->fusion_id = FUSION_ID_MASTER;

     /* Create the main pool. */
     ret = fusion_shm_pool_create( world, "Fusion Main Pool", 0x100000,
                                   fusion_config->debugshm, &world->shared->main_pool );
     if (ret)
          goto error;

     D_MAGIC_SET( world, FusionWorld );
     D_MAGIC_SET( world->shared, FusionWorldShared );

     fusion_skirmish_init( &world->shared->arenas_lock, "Fusion Arenas", world );

     world->shared->world = world;

     direct_mutex_init( &world->event_dispatcher_mutex );
     direct_waitqueue_init( &world->event_dispatcher_cond );
     direct_waitqueue_init( &world->event_dispatcher_process_cond );
     direct_mutex_init( &world->event_dispatcher_call_mutex );
     direct_waitqueue_init( &world->event_dispatcher_call_cond );
     world->event_dispatcher_thread = direct_thread_create( DTT_MESSAGING, event_dispatcher_loop, world, "Fusion Dispatch" );

     world->refs = 1;

     *ret_world = world;

     return DR_OK;

error:
     if (world) {
          if (world->shared)
               D_FREE( world->shared );

          D_FREE( world );
     }

     direct_shutdown();

     return ret;
}

DirectResult
fusion_world_activate( FusionWorld *world )
{
     return DR_OK;
}

DirectResult
fusion_dispatch_cleanup_add( FusionWorld                  *world,
                             FusionDispatchCleanupFunc     func,
                             void                         *ctx,
                             FusionDispatchCleanup       **ret_cleanup )
{
     func( ctx );

     return DR_OK;
}

DirectResult
fusion_dispatch_cleanup_remove( FusionWorld                  *world,
                                FusionDispatchCleanup        *cleanup )
{
     return DR_OK;
}

DirectResult
fusion_stop_dispatcher( FusionWorld *world,
                        bool         emergency )
{
     return DR_OK;
}

DirectResult
fusion_dispatch( FusionWorld *world,
                 size_t       buf_size )
{
     D_UNIMPLEMENTED();

     return DR_OK;
}

/*
 * Exits the fusion world.
 *
 * If 'emergency' is true the function won't join but kill the dispatcher thread.
 */
DirectResult
fusion_exit( FusionWorld *world,
             bool         emergency )
{
     D_MAGIC_ASSERT( world, FusionWorld );
     D_MAGIC_ASSERT( world->shared, FusionWorldShared );

     fusion_shm_pool_destroy( world, world->shared->main_pool );

     direct_mutex_lock( &world->event_dispatcher_mutex );
     world->dispatch_stop = 1;
     direct_mutex_unlock( &world->event_dispatcher_mutex );

     direct_waitqueue_signal( &world->event_dispatcher_call_cond );
     direct_waitqueue_signal( &world->event_dispatcher_cond );

     direct_mutex_deinit( &world->event_dispatcher_mutex );
     direct_waitqueue_deinit( &world->event_dispatcher_cond );
     direct_mutex_deinit( &world->event_dispatcher_call_mutex );
     direct_waitqueue_deinit( &world->event_dispatcher_call_cond );

     D_MAGIC_CLEAR( world->shared );

     D_FREE( world->shared );

     D_MAGIC_CLEAR( world );

     D_FREE( world );

     direct_shutdown();

     return DR_OK;
}

/*
 * Sets the fork() action of the calling Fusionee within the world.
 */
void
fusion_world_set_fork_action( FusionWorld      *world,
                              FusionForkAction  action )
{
     D_MAGIC_ASSERT( world, FusionWorld );
}

/*
 * Gets the current fork() action.
 */
FusionForkAction 
fusion_world_get_fork_action( FusionWorld *world )
{
     D_MAGIC_ASSERT( world, FusionWorld );
     
     return world->fork_action;
}

/*
 * Registers a callback called upon fork().
 */
void
fusion_world_set_fork_callback( FusionWorld        *world,
                                FusionForkCallback  callback )
{
     D_MAGIC_ASSERT( world, FusionWorld );
}

/*
 * Registers a callback called when a slave exits.
 */
void
fusion_world_set_leave_callback( FusionWorld         *world,
                                 FusionLeaveCallback  callback,
                                 void                *ctx )
{
     D_MAGIC_ASSERT( world, FusionWorld );
}

/*
 * Return the index of the specified world.
 */
int
fusion_world_index( const FusionWorld *world )
{
     D_MAGIC_ASSERT( world, FusionWorld );

     return 0;
}


/*
 * Return true if this process is the master.
 */
bool
fusion_master( const FusionWorld *world )
{
     D_MAGIC_ASSERT( world, FusionWorld );

     return true;
}

/*
 * Sends a signal to one or more fusionees and optionally waits
 * for their processes to terminate.
 *
 * A fusion_id of zero means all fusionees but the calling one.
 * A timeout of zero means infinite waiting while a negative value
 * means no waiting at all.
 */
DirectResult
fusion_kill( FusionWorld *world,
             FusionID     fusion_id,
             int          signal,
             int          timeout_ms )
{
     D_MAGIC_ASSERT( world, FusionWorld );

     world->dispatch_stop = 1;

     return DR_OK;
}

/*
 * Return the own Fusion ID within the specified world.
 */
FusionID
fusion_id( const FusionWorld *world )
{
     D_MAGIC_ASSERT( world, FusionWorld );

     return world->fusion_id;
}

/*
 * Return if the world is a multi application world.
 */
bool
fusion_is_multi( const FusionWorld *world )
{
     D_MAGIC_ASSERT( world, FusionWorld );

     return false;
}

/*
 * Return the thread ID of the Fusion Dispatcher within the specified world.
 */
pid_t
fusion_dispatcher_tid( const FusionWorld *world )
{
     D_MAGIC_ASSERT( world, FusionWorld );

     return direct_thread_get_tid( world->event_dispatcher_thread );
}

/*
 * Wait until all pending messages are processed.
 */
DirectResult
fusion_sync( const FusionWorld *world )
{
     D_MAGIC_ASSERT( world, FusionWorld );

     return DR_OK;
}

/* Check if a pointer points to the shared memory. */
bool
fusion_is_shared( FusionWorld *world,
                  const void  *ptr )
{
     D_MAGIC_ASSERT( world, FusionWorld );

     return true;
}

const char *
fusion_get_tmpfs( FusionWorld *world )
{
     D_MAGIC_ASSERT( world, FusionWorld );
     D_MAGIC_ASSERT( world->shared, FusionWorldShared );

     return "/tmp";
}

DirectResult
fusion_get_fusionee_path( const FusionWorld *world,
                          FusionID           fusion_id,
                          char              *buf,
                          size_t             buf_size,
                          size_t            *ret_size )
{
     D_UNIMPLEMENTED();

     return DR_UNIMPLEMENTED;
}

DirectResult
fusion_get_fusionee_pid( const FusionWorld *world,
                         FusionID           fusion_id,
                         pid_t             *ret_pid )
{
     D_UNIMPLEMENTED();

     return DR_UNIMPLEMENTED;
}

DirectResult
fusion_world_set_root( FusionWorld *world,
                       void        *root )
{
     D_ASSERT( world != NULL );
     D_ASSERT( world->shared != NULL );

     if (world->fusion_id != FUSION_ID_MASTER)
          return DR_ACCESSDENIED;

     world->shared->world_root = root;

     return DR_OK;
}

void *
fusion_world_get_root( FusionWorld *world )
{
     D_ASSERT( world != NULL );
     D_ASSERT( world->shared != NULL );

     return world->shared->world_root;
}

#endif

