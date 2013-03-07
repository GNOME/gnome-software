## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

AM_CPPFLAGS =						\
	$(GLIB_CFLAGS)					\
	$(GTK_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	$(PACKAGEKIT_CFLAGS)				\
	-DBINDIR=\"$(bindir)\"				\
	-DDATADIR=\"$(datadir)\"			\
	-DG_LOG_DOMAIN=\"GsPlugin\"			\
	-DLIBDIR=\""$(libdir)"\"			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\"		\
	-DSBINDIR=\"$(sbindir)\"			\
	-DSYSCONFDIR=\""$(sysconfdir)"\"		\
	-DTESTDATADIR=\""$(top_srcdir)/data/tests"\"	\
	-I$(top_srcdir)/src

plugindir = $(libdir)/gs-plugins
plugin_LTLIBRARIES =					\
	libgs_plugin_datadir_apps.la			\
	libgs_plugin_desktopdb.la			\
	libgs_plugin_dummy.la				\
	libgs_plugin_hardcoded-kind.la			\
	libgs_plugin_hardcoded-popular.la		\
	libgs_plugin_hardcoded-ratings.la		\
	libgs_plugin_packagekit.la

libgs_plugin_dummy_la_SOURCES = gs-plugin-dummy.c
libgs_plugin_dummy_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_dummy_la_LDFLAGS = -module -avoid-version
libgs_plugin_dummy_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_hardcoded_kind_la_SOURCES = gs-plugin-hardcoded-kind.c
libgs_plugin_hardcoded_kind_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_hardcoded_kind_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_kind_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_hardcoded_popular_la_SOURCES = gs-plugin-hardcoded-popular.c
libgs_plugin_hardcoded_popular_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_hardcoded_popular_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_popular_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_hardcoded_ratings_la_SOURCES = gs-plugin-hardcoded-ratings.c
libgs_plugin_hardcoded_ratings_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_hardcoded_ratings_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_ratings_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_packagekit_la_SOURCES = gs-plugin-packagekit.c
libgs_plugin_packagekit_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_desktopdb_la_SOURCES = gs-plugin-desktopdb.c
libgs_plugin_desktopdb_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_desktopdb_la_LDFLAGS = -module -avoid-version
libgs_plugin_desktopdb_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_datadir_apps_la_SOURCES = gs-plugin-datadir-apps.c
libgs_plugin_datadir_apps_la_LIBADD = $(GS_PLUGIN_LIBS) $(GTK_LIBS)
libgs_plugin_datadir_apps_la_LDFLAGS = -module -avoid-version
libgs_plugin_datadir_apps_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

-include $(top_srcdir)/git.mk
