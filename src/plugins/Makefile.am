## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

AM_CPPFLAGS =						\
	$(GLIB_CFLAGS)					\
	$(GTK_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	$(PACKAGEKIT_CFLAGS)				\
	$(SQLITE_CFLAGS)				\
	-DBINDIR=\"$(bindir)\"				\
	-DDATADIR=\"$(datadir)\"			\
	-DG_LOG_DOMAIN=\"GsPlugin\"			\
	-DLIBDIR=\""$(libdir)"\"			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\"		\
	-DSBINDIR=\"$(sbindir)\"			\
	-DSYSCONFDIR=\""$(sysconfdir)"\"		\
	-DTESTDATADIR=\""$(top_srcdir)/data/tests"\"	\
	-I$(top_srcdir)/src

resourcesdir = $(datadir)/gnome-software
dist_resources_DATA =					\
	data/cheese.png					\
	data/gedit.png					\
	data/gimp.png					\
	data/transmission-gtk.png

plugindir = $(libdir)/gs-plugins
plugin_LTLIBRARIES =					\
	libgs_plugin_appstream.la			\
	libgs_plugin_datadir_apps.la			\
	libgs_plugin_datadir_filename.la		\
	libgs_plugin_datadir_filename_local.la		\
	libgs_plugin_desktopdb.la			\
	libgs_plugin_dummy.la				\
	libgs_plugin_self_test.la			\
	libgs_plugin_hardcoded-descriptions.la		\
	libgs_plugin_hardcoded-featured.la		\
	libgs_plugin_hardcoded-kind.la			\
	libgs_plugin_hardcoded-popular.la		\
	libgs_plugin_hardcoded-menu-spec.la		\
	libgs_plugin_hardcoded-ratings.la		\
	libgs_plugin_hardcoded-screenshots.la		\
	libgs_plugin_local-ratings.la			\
	libgs_plugin_packagekit-refine.la		\
	libgs_plugin_packagekit.la

libgs_plugin_dummy_la_SOURCES = gs-plugin-dummy.c
libgs_plugin_dummy_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_dummy_la_LDFLAGS = -module -avoid-version
libgs_plugin_dummy_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_self_test_la_SOURCES = gs-plugin-self-test.c
libgs_plugin_self_test_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_self_test_la_LDFLAGS = -module -avoid-version
libgs_plugin_self_test_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_appstream_la_SOURCES = gs-plugin-appstream.c gs-appstream-item.c gs-appstream-item.h
libgs_plugin_appstream_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_appstream_la_LDFLAGS = -module -avoid-version
libgs_plugin_appstream_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_hardcoded_kind_la_SOURCES = gs-plugin-hardcoded-kind.c
libgs_plugin_hardcoded_kind_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_hardcoded_kind_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_kind_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_hardcoded_popular_la_SOURCES = gs-plugin-hardcoded-popular.c
libgs_plugin_hardcoded_popular_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_hardcoded_popular_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_popular_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_hardcoded_menu_spec_la_SOURCES = gs-plugin-hardcoded-menu-spec.c
libgs_plugin_hardcoded_menu_spec_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_hardcoded_menu_spec_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_menu_spec_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_hardcoded_descriptions_la_SOURCES = gs-plugin-hardcoded-descriptions.c
libgs_plugin_hardcoded_descriptions_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_hardcoded_descriptions_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_descriptions_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_hardcoded_featured_la_SOURCES = gs-plugin-hardcoded-featured.c
libgs_plugin_hardcoded_featured_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_hardcoded_featured_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_featured_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_hardcoded_ratings_la_SOURCES = gs-plugin-hardcoded-ratings.c
libgs_plugin_hardcoded_ratings_la_LIBADD = $(GS_PLUGIN_LIBS) $(SQLITE_LIBS)
libgs_plugin_hardcoded_ratings_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_ratings_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_hardcoded_screenshots_la_SOURCES = gs-plugin-hardcoded-screenshots.c
libgs_plugin_hardcoded_screenshots_la_LIBADD = $(GS_PLUGIN_LIBS) $(SQLITE_LIBS)
libgs_plugin_hardcoded_screenshots_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_screenshots_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_local_ratings_la_SOURCES = gs-plugin-local-ratings.c
libgs_plugin_local_ratings_la_LIBADD = $(GS_PLUGIN_LIBS) $(SQLITE_LIBS)
libgs_plugin_local_ratings_la_LDFLAGS = -module -avoid-version
libgs_plugin_local_ratings_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_packagekit_la_SOURCES = gs-plugin-packagekit.c
libgs_plugin_packagekit_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_packagekit_refine_la_SOURCES = gs-plugin-packagekit-refine.c
libgs_plugin_packagekit_refine_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_refine_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_refine_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_desktopdb_la_SOURCES = gs-plugin-desktopdb.c
libgs_plugin_desktopdb_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_desktopdb_la_LDFLAGS = -module -avoid-version
libgs_plugin_desktopdb_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_datadir_apps_la_SOURCES = gs-plugin-datadir-apps.c
libgs_plugin_datadir_apps_la_LIBADD = $(GS_PLUGIN_LIBS) $(GTK_LIBS)
libgs_plugin_datadir_apps_la_LDFLAGS = -module -avoid-version
libgs_plugin_datadir_apps_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_datadir_filename_la_SOURCES = gs-plugin-datadir-filename.c
libgs_plugin_datadir_filename_la_LIBADD = $(GS_PLUGIN_LIBS) $(GTK_LIBS)
libgs_plugin_datadir_filename_la_LDFLAGS = -module -avoid-version
libgs_plugin_datadir_filename_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_datadir_filename_local_la_SOURCES = gs-plugin-datadir-filename-local.c
libgs_plugin_datadir_filename_local_la_LIBADD = $(GS_PLUGIN_LIBS) $(GTK_LIBS)
libgs_plugin_datadir_filename_local_la_LDFLAGS = -module -avoid-version
libgs_plugin_datadir_filename_local_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

-include $(top_srcdir)/git.mk
