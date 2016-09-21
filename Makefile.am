ACLOCAL_AMFLAGS = -I m4
AM_CFLAGS = @VKD3D_CFLAGS@
AM_CPPFLAGS = -I$(srcdir)/include -I$(srcdir)/include/dummy

widl_headers = \
	include/d3d12.h

vkd3d_public_headers = \
	include/d3d12.h \
	include/dummy/rpc.h \
	include/dummy/rpcndr.h \
	include/dxgibase.h \
	include/vkd3d_windows.h

BUILT_SOURCES = $(widl_headers)
CLEANFILES = $(widl_headers)

lib_LTLIBRARIES = libvkd3d.la
libvkd3d_la_SOURCES = \
	libs/vkd3d/debug.c \
	libs/vkd3d/device.c \
	libs/vkd3d/utils.c \
	libs/vkd3d/vkd3d_main.c

pkgconfigdir = $(libdir)/pkgconfig
pkginclude_HEADERS = $(vkd3d_public_headers)
nodist_pkgconfig_DATA = libvkd3d.pc

$(widl_headers): %.h: %.idl
	$(WIDL) -o $@ $<

libvkd3d.pc: $(srcdir)/libs/vkd3d/libvkd3d.pc.in
	sed -e 's![@]prefix[@]!$(prefix)!g' \
		-e 's![@]exec_prefix[@]!$(exec_prefix)!g' \
		-e 's![@]includedir[@]!$(includedir)!g' \
		-e 's![@]libdir[@]!$(libdir)!g' \
		-e 's![@]PACKAGE_VERSION[@]!$(PACKAGE_VERSION)!g' \
		$< > $@
