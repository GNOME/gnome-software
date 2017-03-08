AM_CPPFLAGS =						\
	-DG_LOG_DOMAIN=\"GsPluginRepos\"		\
	-DTESTDATADIR=\""$(srcdir)/tests"\"		\
	-DLOCALPLUGINDIR=\""$(builddir)/.libs"\"

plugindir = $(GS_PLUGIN_DIR)
plugin_LTLIBRARIES = libgs_plugin_repos.la

libgs_plugin_repos_la_SOURCES = gs-plugin-repos.c
libgs_plugin_repos_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_repos_la_LDFLAGS = -module -avoid-version
libgs_plugin_repos_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

if ENABLE_TESTS
check_PROGRAMS = gs-self-test
gs_self_test_SOURCES = gs-self-test.c
gs_self_test_LDADD = $(GS_PRIVATE_LIBS)
gs_self_test_CFLAGS = $(GS_PRIVATE_CFLAGS)
TESTS = gs-self-test
endif

EXTRA_DIST = tests/yum.repos.d/utopia.repo

-include $(top_srcdir)/git.mk
