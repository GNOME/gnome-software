AM_CPPFLAGS =						\
	-DG_LOG_DOMAIN=\"GsPluginFlatpak\"		\
	-DSYSCONFDIR=\""$(sysconfdir)"\"		\
	-DTESTDATADIR=\""$(srcdir)/tests"\"		\
	-DLOCALPLUGINDIR=\""$(builddir)/.libs"\"	\
	-DLOCALPLUGINDIR_CORE=\""$(top_builddir)/plugins/core/.libs"\"

plugindir = $(GS_PLUGIN_DIR)
plugin_LTLIBRARIES = libgs_plugin_flatpak.la

libgs_plugin_flatpak_la_SOURCES =			\
	gs-appstream.c					\
	gs-appstream.h					\
	gs-flatpak.c					\
	gs-flatpak.h					\
	gs-flatpak-symlinks.c				\
	gs-flatpak-symlinks.h				\
	gs-plugin-flatpak.c
libgs_plugin_flatpak_la_LIBADD = $(GS_PLUGIN_LIBS) $(FLATPAK_LIBS)
libgs_plugin_flatpak_la_LDFLAGS = -module -avoid-version
libgs_plugin_flatpak_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(FLATPAK_CFLAGS)

%.metainfo.xml: %.metainfo.xml.in
	$(AM_V_GEN) msgfmt --xml -d $(top_srcdir)/po --template $< -o $@

appdata_in_files = org.gnome.Software.Plugin.Flatpak.metainfo.xml.in
appdatadir = $(datadir)/appdata
appdata_DATA = $(appdata_in_files:.xml.in=.xml)
EXTRA_DIST = $(appdata_in_files)
CLEANFILES = $(appdata_DATA)

if ENABLE_TESTS
check_PROGRAMS = gs-self-test
gs_self_test_SOURCES = gs-self-test.c
gs_self_test_LDADD = $(top_builddir)/lib/libgnomesoftware.a $(GS_PRIVATE_LIBS)
gs_self_test_CFLAGS = $(GS_PRIVATE_CFLAGS)
TESTS = gs-self-test

tests/app-with-runtime/repo: tests/app-with-runtime
	$(AM_V_GEN) cd "$(srcdir)/tests" && ./build.py && cd -

BUILT_SOURCES =						\
	tests/app-with-runtime/repo

endif

-include $(top_srcdir)/git.mk
