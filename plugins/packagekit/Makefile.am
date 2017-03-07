AM_CPPFLAGS =						\
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
	-DTESTDATADIR=\""$(srcdir)"\"			\
	-DLOCALPLUGINDIR=\""$(builddir)/.libs"\"

plugindir = $(GS_PLUGIN_DIR)
plugin_LTLIBRARIES =					\
	libgs_plugin_systemd-updates.la			\
	libgs_plugin_packagekit-local.la		\
	libgs_plugin_packagekit-refine.la		\
	libgs_plugin_packagekit-refresh.la		\
	libgs_plugin_packagekit-offline.la		\
	libgs_plugin_packagekit-origin.la		\
	libgs_plugin_packagekit-proxy.la		\
	libgs_plugin_packagekit-history.la		\
	libgs_plugin_packagekit-upgrade.la		\
	libgs_plugin_packagekit.la

libgs_plugin_packagekit_la_SOURCES =			\
	gs-plugin-packagekit.c				\
	packagekit-common.c				\
	packagekit-common.h
libgs_plugin_packagekit_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(PACKAGEKIT_CFLAGS)

libgs_plugin_packagekit_refine_la_SOURCES =		\
	gs-plugin-packagekit-refine.c			\
	gs-markdown.c					\
	gs-markdown.h					\
	packagekit-common.c				\
	packagekit-common.h
libgs_plugin_packagekit_refine_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_refine_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_refine_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(PACKAGEKIT_CFLAGS)

libgs_plugin_packagekit_refresh_la_SOURCES =		\
	gs-plugin-packagekit-refresh.c			\
	packagekit-common.c				\
	packagekit-common.h
libgs_plugin_packagekit_refresh_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_refresh_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_refresh_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(PACKAGEKIT_CFLAGS)

libgs_plugin_packagekit_local_la_SOURCES =		\
	gs-plugin-packagekit-local.c			\
	packagekit-common.c				\
	packagekit-common.h
libgs_plugin_packagekit_local_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_local_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_local_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(PACKAGEKIT_CFLAGS)

libgs_plugin_systemd_updates_la_SOURCES =		\
	gs-plugin-systemd-updates.c			\
	packagekit-common.c				\
	packagekit-common.h
libgs_plugin_systemd_updates_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_systemd_updates_la_LDFLAGS = -module -avoid-version
libgs_plugin_systemd_updates_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(PACKAGEKIT_CFLAGS)

libgs_plugin_packagekit_history_la_SOURCES = gs-plugin-packagekit-history.c
libgs_plugin_packagekit_history_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_history_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_history_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(PACKAGEKIT_CFLAGS)

libgs_plugin_packagekit_upgrade_la_SOURCES =		\
	gs-plugin-packagekit-upgrade.c			\
	packagekit-common.c				\
	packagekit-common.h
libgs_plugin_packagekit_upgrade_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_upgrade_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_upgrade_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(PACKAGEKIT_CFLAGS)

libgs_plugin_packagekit_offline_la_SOURCES = gs-plugin-packagekit-offline.c
libgs_plugin_packagekit_offline_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_offline_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_offline_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(PACKAGEKIT_CFLAGS)

libgs_plugin_packagekit_origin_la_SOURCES =		\
	gs-plugin-packagekit-origin.c			\
	packagekit-common.c				\
	packagekit-common.h
libgs_plugin_packagekit_origin_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_origin_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_origin_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(PACKAGEKIT_CFLAGS)

libgs_plugin_packagekit_proxy_la_SOURCES = gs-plugin-packagekit-proxy.c
libgs_plugin_packagekit_proxy_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_proxy_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_proxy_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(PACKAGEKIT_CFLAGS)

if ENABLE_TESTS
check_PROGRAMS = gs-self-test
gs_self_test_SOURCES = gs-self-test.c gs-markdown.c
gs_self_test_LDADD = $(GS_PRIVATE_LIBS)
gs_self_test_CFLAGS = $(GS_PRIVATE_CFLAGS)
TESTS = gs-self-test
endif

EXTRA_DIST = tests/chiron-1.1-1.fc24.x86_64.rpm

-include $(top_srcdir)/git.mk
