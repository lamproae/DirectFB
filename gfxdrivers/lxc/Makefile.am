## Makefile.am for DirectFB/src/core/gfxcards/lxc

INCLUDES = \
	-I$(top_builddir)/include	\
	-I$(top_builddir)/lib	\
	-I$(top_srcdir)/include	\
	-I$(top_srcdir)/lib	\
	-I$(top_srcdir)/src	\
	-I$(top_srcdir)/systems

lxc_LTLIBRARIES = libdirectfb_lxc.la

if BUILD_STATIC
lxc_DATA = $(lxc_LTLIBRARIES:.la=.o)
endif

lxcdir = $(MODULEDIR)/gfxdrivers

libdirectfb_lxc_la_SOURCES =	\
	lxc_gfxdriver.c		\
	lxc_gfxdriver.h		\
	lxc_layer.c		\
	lxc_layer.h		\
	lxc_screen.c		\
	lxc_screen.h

libdirectfb_lxc_la_LDFLAGS = \
	-module			\
	-avoid-version		\
	$(DFB_LDFLAGS) -lm

libdirectfb_lxc_la_LIBADD = \
	$(top_builddir)/lib/direct/libdirect.la \
	$(top_builddir)/src/libdirectfb.la


include $(top_srcdir)/rules/libobject.make
	
