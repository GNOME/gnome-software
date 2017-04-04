
AM_CPPFLAGS =						\
	-DG_LOG_DOMAIN=\"Gs\"				\
	-DG_LOG_USE_STRUCTURED				\
	-DDATADIR=\"$(datadir)\"			\
	-DLIBDIR=\"$(libdir)\"				\
	-DLOCALEDIR=\""$(localedir)"\"			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\"		\
	-DTESTDATADIR=\""$(top_srcdir)/data"\"		\
	-DLOCALPLUGINDIR=\""$(top_builddir)/plugins/.libs"\"

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gnome-software.pc

gnomesoftware_includedir = $(includedir)/gnome-software
gnomesoftware_include_HEADERS =				\
	gnome-software.h				\
	gs-app.h					\
	gs-app-list.h					\
	gs-auth.h					\
	gs-category.h					\
	gs-os-release.h					\
	gs-plugin-event.h				\
	gs-plugin.h					\
	gs-plugin-types.h				\
	gs-plugin-vfuncs.h				\
	gs-utils.h

noinst_LIBRARIES = libgnomesoftware.a
libgnomesoftware_a_SOURCES =				\
	gnome-software.h				\
	gnome-software-private.h			\
	gs-app.c					\
	gs-app.h					\
	gs-app-list.c					\
	gs-app-list.h					\
	gs-app-list-private.h				\
	gs-app-private.h				\
	gs-auth.c					\
	gs-auth.h					\
	gs-category.c					\
	gs-category.h					\
	gs-category-private.h				\
	gs-debug.c					\
	gs-debug.h					\
	gs-os-release.c					\
	gs-os-release.h					\
	gs-plugin.c					\
	gs-plugin-event.c				\
	gs-plugin-event.h				\
	gs-plugin.h					\
	gs-plugin-loader.c				\
	gs-plugin-loader.h				\
	gs-plugin-loader-sync.c				\
	gs-plugin-loader-sync.h				\
	gs-plugin-private.h				\
	gs-plugin-types.h				\
	gs-plugin-vfuncs.h				\
	gs-test.c					\
	gs-test.h					\
	gs-utils.c					\
	gs-utils.h

libgnomesoftware_a_CFLAGS = $(GS_PRIVATE_CFLAGS)

libexec_PROGRAMS =					\
	gnome-software-cmd

gnome_software_cmd_SOURCES =				\
	gs-cmd.c

gnome_software_cmd_LDADD =				\
	libgnomesoftware.a				\
	$(GS_PRIVATE_LIBS)

gnome_software_cmd_CFLAGS =				\
	$(GS_PRIVATE_CFLAGS)

gnome_software_cmd_LDFLAGS =				\
	$(PIE_LDFLAGS)					\
	$(RELRO_LDFLAGS)

EXTRA_DIST =						\
	gnome-software.pc.in

TESTS_ENVIRONMENT =					\
	$(top_builddir)/libtool				\
	--mode=execute valgrind				\
	--quiet						\
	--leak-check=full				\
	--show-possibly-lost=no

if ENABLE_TESTS
check_PROGRAMS = gs-self-test
gs_self_test_SOURCES = gs-self-test.c
gs_self_test_LDADD = libgnomesoftware.a $(GS_PRIVATE_LIBS)
gs_self_test_CFLAGS = $(GS_PRIVATE_CFLAGS)
TESTS = gs-self-test
endif

-include $(top_srcdir)/git.mk
