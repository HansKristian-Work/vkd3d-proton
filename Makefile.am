ACLOCAL_AMFLAGS = -I m4
AM_CFLAGS = @VKD3D_CFLAGS@
AM_CPPFLAGS = -I$(srcdir)/include -I$(srcdir)/include/dummy

widl_headers = \
	include/d3d12.h

BUILT_SOURCES = $(widl_headers)
CLEANFILES = $(widl_headers)

lib_LTLIBRARIES = libvkd3d.la
libvkd3d_la_SOURCES = \
	libs/vkd3d/vkd3d_main.c

$(widl_headers): %.h: %.idl
	$(WIDL) -o $@ $<
