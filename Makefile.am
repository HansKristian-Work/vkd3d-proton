ACLOCAL_AMFLAGS = -I m4
AM_CFLAGS = @VKD3D_CFLAGS@
AM_CPPFLAGS = -I$(srcdir)/include -I$(srcdir)/include/dummy -I$(srcdir)/include/private

widl_headers = \
	include/d3d12.h

vkd3d_public_headers = \
	include/d3d12.h \
	include/dummy/rpc.h \
	include/dummy/rpcndr.h \
	include/dxgibase.h \
	include/vkd3d_windows.h

vkd3d_tests = \
	tests/d3d12

BUILT_SOURCES = $(widl_headers)
CLEANFILES = $(widl_headers)

lib_LTLIBRARIES = libvkd3d.la
libvkd3d_la_SOURCES = \
	libs/vkd3d/command.c \
	libs/vkd3d/debug.c \
	libs/vkd3d/device.c \
	libs/vkd3d/resource.c \
	libs/vkd3d/state.c \
	libs/vkd3d/utils.c \
	libs/vkd3d/vkd3d_main.c

pkgconfigdir = $(libdir)/pkgconfig
pkginclude_HEADERS = $(vkd3d_public_headers)
nodist_pkgconfig_DATA = libvkd3d.pc

LDADD = libvkd3d.la
check_PROGRAMS =  $(vkd3d_tests)
AM_DEFAULT_SOURCE_EXT = .c
TESTS = $(vkd3d_tests)

$(widl_headers): %.h: %.idl
	$(WIDL) -o $@ $<

libvkd3d.pc: $(srcdir)/libs/vkd3d/libvkd3d.pc.in
	sed -e 's![@]prefix[@]!$(prefix)!g' \
		-e 's![@]exec_prefix[@]!$(exec_prefix)!g' \
		-e 's![@]includedir[@]!$(includedir)!g' \
		-e 's![@]libdir[@]!$(libdir)!g' \
		-e 's![@]PACKAGE_VERSION[@]!$(PACKAGE_VERSION)!g' \
		$< > $@

## Cross-compile tests
cross_implibs = crosslibs/d3d12
CROSS_CPPFLAGS = -I$(srcdir)/include -I$(srcdir)/include/private -I$(builddir)/include

if HAS_CROSSTARGET32
CROSS32_CC = @CROSSCC32@
CROSS32_DLLTOOL = @CROSSTARGET32@-dlltool
CROSS32_IMPLIBS = $(cross_implibs:=.cross32.a)
CROSS32_EXEFILES = $(vkd3d_tests:=.cross32.exe)
CROSS32_FILES = $(CROSS32_IMPLIBS) $(CROSS32_EXEFILES)

CLEANFILES += $(CROSS32_FILES)
crosstest32: $(widl_headers) $(CROSS32_FILES)

$(CROSS32_IMPLIBS): %.cross32.a: %.cross32.def
	${MKDIR_P} crosslibs
	$(CROSS32_DLLTOOL) -k -m i386 --as-flags=-32 -d $< -l $@

$(CROSS32_EXEFILES): %.cross32.exe: %.c $(CROSS32_IMPLIBS)
	$(CROSS32_CC) $(CROSS_CPPFLAGS) -o $@ $< $(CROSS32_IMPLIBS)
else
crosstest32:
endif

if HAS_CROSSTARGET64
CROSS64_CC = @CROSSCC64@
CROSS64_DLLTOOL = @CROSSTARGET64@-dlltool
CROSS64_IMPLIBS = $(cross_implibs:=.cross64.a)
CROSS64_EXEFILES = $(vkd3d_tests:=.cross64.exe)
CROSS64_FILES = $(CROSS64_IMPLIBS) $(CROSS64_EXEFILES)

CLEANFILES += $(CROSS64_FILES)
crosstest64: $(widl_headers) $(CROSS64_FILES)

$(CROSS64_IMPLIBS): %.cross64.a: %.cross64.def
	${MKDIR_P} crosslibs
	$(CROSS64_DLLTOOL) -k -m i386:x86-64 --as-flags=-64 -d $< -l $@

$(CROSS64_EXEFILES): %.cross64.exe: %.c $(CROSS64_IMPLIBS)
	$(CROSS64_CC) $(CROSS_CPPFLAGS) -o $@ $< $(CROSS64_IMPLIBS)
else
crosstest64:
endif

.PHONY: crosstest crosstest32 crosstest64
crosstest: crosstest32 crosstest64
