/*
   (c) Copyright 2012-2013  DirectFB integrated media GmbH
   (c) Copyright 2001-2013  The world wide DirectFB Open Source Community (directfb.org)
   (c) Copyright 2000-2004  Convergence (integrated media) GmbH

   All rights reserved.

   Written by Denis Oliver Kropp <dok@directfb.org>,
              Andreas Hundt <andi@fischlustig.de>,
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

#include <stdio.h>
#include <string.h>

extern "C" {
#include <direct/messages.h>
}

#include <egl/dfbegl.h>


namespace DirectFB {

namespace EGL {

TLS::TLS()
     :
     egl_error( EGL_SUCCESS ),
     api( EGL_OPENGL_ES_API ),
     context( NULL ),
     draw( NULL ),
     read( NULL )
{

}

EGLint
TLS::GetError()
{
    return egl_error;
}

void
TLS::SetError( EGLint egl_error )
{
    this->egl_error = egl_error;
}

Context *
TLS::GetContext()
{
    return context;
}

void
TLS::SetContext( Context *context )
{
    this->context = context;
}

Surface *
TLS::GetDraw()
{
    return draw;
}

void
TLS::SetDraw( Surface *draw )
{
    this->draw = draw;
}

Surface *
TLS::GetRead()
{
    return read;
}

void
TLS::SetRead( Surface *draw )
{
    this->read = read;
}

EGLenum
TLS::GetAPI()
{
     return api;
}

void
TLS::SetAPI( EGLenum api )
{
     this->api = api;
}


}

}

