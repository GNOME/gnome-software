AM_CPPFLAGS =						\
	-DG_LOG_DOMAIN=\"GsPluginEpiphany\"		\
	-DTESTDATADIR=\""$(top_srcdir)/data"\"		\
	-DLOCALPLUGINDIR=\""$(builddir)/.libs"\"	\
	-DLOCALPLUGINDIR_CORE=\""$(top_builddir)/plugins/core/.libs"\"

plugindir = $(GS_PLUGIN_DIR)
plugin_LTLIBRARIES = libgs_plugin_epiphany.la

libgs_plugin_epiphany_la_SOURCES = gs-plugin-epiphany.c
libgs_plugin_epiphany_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_epiphany_la_LDFLAGS = -module -avoid-version
libgs_plugin_epiphany_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

%.metainfo.xml: %.metainfo.xml.in
	$(AM_V_GEN) msgfmt --xml -d $(top_srcdir)/po --template $< -o $@

appdata_in_files = org.gnome.Software.Plugin.Epiphany.metainfo.xml.in
appdatadir = $(datadir)/appdata
appdata_DATA = $(appdata_in_files:.xml.in=.xml)
EXTRA_DIST = $(appdata_in_files)
CLEANFILES = $(appdata_DATA)

if ENABLE_TESTS
check_PROGRAMS = gs-self-test
gs_self_test_SOURCES = gs-self-test.c
gs_self_test_LDADD = $(GS_PRIVATE_LIBS)
gs_self_test_CFLAGS = $(GS_PRIVATE_CFLAGS)
TESTS = gs-self-test
endif

-include $(top_srcdir)/git.mk
