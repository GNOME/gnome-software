AM_CPPFLAGS =						\
	-DG_LOG_DOMAIN=\"GsPluginModalias\"		\
	-DTESTDATADIR=\""$(srcdir)/tests"\"		\
	-DLOCALPLUGINDIR=\""$(builddir)/.libs"\"	\
	-DLOCALPLUGINDIR_CORE=\""$(top_builddir)/plugins/core/.libs"\"\
	-DLOCALPLUGINDIR_DUMMY=\""$(top_builddir)/plugins/dummy/.libs"\"

plugindir = $(GS_PLUGIN_DIR)
plugin_LTLIBRARIES = libgs_plugin_modalias.la

libgs_plugin_modalias_la_SOURCES = gs-plugin-modalias.c
libgs_plugin_modalias_la_LIBADD = $(GS_PLUGIN_LIBS) $(GUDEV_LIBS)
libgs_plugin_modalias_la_LDFLAGS = -module -avoid-version
libgs_plugin_modalias_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(GUDEV_CFLAGS)

if ENABLE_TESTS
check_PROGRAMS = gs-self-test
gs_self_test_SOURCES = gs-self-test.c
gs_self_test_LDADD = $(top_builddir)/lib/libgnomesoftware.a $(GS_PRIVATE_LIBS)
gs_self_test_CFLAGS = $(GS_PRIVATE_CFLAGS)
TESTS = gs-self-test
endif

-include $(top_srcdir)/git.mk
