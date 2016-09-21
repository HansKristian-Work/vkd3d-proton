ACLOCAL_AMFLAGS = -I m4

widl_headers = \
	include/d3d12.h

BUILT_SOURCES = $(widl_headers)
CLEANFILES = $(widl_headers)

$(widl_headers): %.h: %.idl
	$(WIDL) -o $@ $<
