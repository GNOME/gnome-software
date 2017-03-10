AM_CPPFLAGS =						\
	-DG_LOG_DOMAIN=\"GsPluginShellExtensions\"	\
	-DTESTDATADIR=\""$(srcdir)/tests"\"		\
	-DLOCALPLUGINDIR=\""$(builddir)/.libs"\"

plugindir = $(GS_PLUGIN_DIR)
plugin_LTLIBRARIES = libgs_plugin_shell-extensions.la
libgs_plugin_shell_extensions_la_SOURCES = gs-plugin-shell-extensions.c
libgs_plugin_shell_extensions_la_LIBADD = $(GS_PLUGIN_LIBS) $(JSON_GLIB_LIBS)
libgs_plugin_shell_extensions_la_LDFLAGS = -module -avoid-version
libgs_plugin_shell_extensions_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(JSON_GLIB_CFLAGS)

if ENABLE_TESTS
check_PROGRAMS = gs-self-test
gs_self_test_SOURCES = gs-self-test.c
gs_self_test_LDADD = $(top_builddir)/lib/libgnomesoftware.a $(GS_PRIVATE_LIBS)
gs_self_test_CFLAGS = $(GS_PRIVATE_CFLAGS)
TESTS = gs-self-test
endif

#EXTRA_DIST = tests/chiron-1.1-1.fc24.x86_64.rpm

-include $(top_srcdir)/git.mk
