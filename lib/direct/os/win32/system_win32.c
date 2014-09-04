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

#include <errno.h>
#include <signal.h>

#include <direct/atomic.h>
#include <direct/debug.h>
#include <direct/signals.h>
#include <direct/system.h>
#include <direct/util.h>

D_LOG_DOMAIN( Direct_Futex, "Direct/Futex", "Direct Futex" );
D_LOG_DOMAIN( Direct_Trap,  "Direct/Trap",  "Direct Trap" );

#define PAGESIZE 4096
/**********************************************************************************************************************/

long
direct_pagesize( void )
{
     return PAGESIZE;
}

unsigned long
direct_page_align( unsigned long value )
{
     unsigned long mask = PAGESIZE -1;

     return (value + mask) & ~mask;
}

/**********************************************************************************************************************/

pid_t
direct_getpid( void )
{
     return GetCurrentThreadId();
}

__dfb_no_instrument_function__
pid_t
direct_gettid( void )
{
     return GetCurrentThreadId();
}

/**********************************************************************************************************************/

DirectResult
direct_tgkill( int tgid, int tid, int sig )
{
     return DR_OK;
}

void
direct_trap( const char *domain, int sig )
{
     _exit(0);
}

/**********************************************************************************************************************/

DirectResult
direct_kill( pid_t pid, int sig )
{
     return DR_UNSUPPORTED;
}

void
direct_sync()
{
}

DirectResult
direct_socketpair( int __domain, int __type, int __protocol, int __fds[2] )
{
     return DR_UNSUPPORTED;
}

DirectResult
direct_sigprocmask( int __how, const sigset_t *__set,
                    sigset_t *__oset )
{
     return DR_UNSUPPORTED;
}

uid_t
direct_getuid()
{
     return 0;
}

uid_t
direct_geteuid()
{
     return 0;
}

char *
direct_getenv( const char *name )
{
     return getenv( name );
}

/**********************************************************************************************************************/

DirectResult
direct_futex( int *uaddr, int op, int val, const struct timespec *timeout, int *uaddr2, int val3 )
{
     return DR_OK;
}

