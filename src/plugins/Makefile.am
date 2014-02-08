## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

AM_CPPFLAGS =						\
	$(GLIB_CFLAGS)					\
	$(GTK_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	$(PACKAGEKIT_CFLAGS)				\
	$(SOUP_CFLAGS)					\
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

noinst_LTLIBRARIES =					\
	libgs_plugin_dummy.la				\
	libgs_plugin_self_test.la

plugindir = $(libdir)/gs-plugins-${GS_PLUGIN_API_VERSION}
plugin_LTLIBRARIES =					\
	libgs_plugin_appdata.la				\
	libgs_plugin_appstream.la			\
	libgs_plugin_datadir_apps.la			\
	libgs_plugin_datadir_filename.la		\
	libgs_plugin_datadir_filename_local.la		\
	libgs_plugin_desktopdb.la			\
	libgs_plugin_hardcoded-featured.la		\
	libgs_plugin_moduleset-gnome.la			\
	libgs_plugin_hardcoded-categories.la		\
	libgs_plugin_hardcoded-popular.la		\
	libgs_plugin_menu-spec-categories.la		\
	libgs_plugin_menu-spec-refine.la		\
	libgs_plugin_local-ratings.la			\
	libgs_plugin_fedora_tagger_ratings.la		\
	libgs_plugin_fedora_tagger_usage.la		\
	libgs_plugin_epiphany.la			\
	libgs_plugin_systemd-updates.la			\
	libgs_plugin_packagekit-search.la		\
	libgs_plugin_packagekit-refine.la		\
	libgs_plugin_packagekit-refresh.la		\
	libgs_plugin_packagekit-updates.la		\
	libgs_plugin_packagekit-offline.la		\
	libgs_plugin_packagekit-history.la		\
	libgs_plugin_packagekit.la

libgs_plugin_dummy_la_SOURCES = gs-plugin-dummy.c
libgs_plugin_dummy_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_dummy_la_LDFLAGS = -module -avoid-version
libgs_plugin_dummy_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_fedora_tagger_ratings_la_SOURCES = gs-plugin-fedora-tagger-ratings.c
libgs_plugin_fedora_tagger_ratings_la_LIBADD = $(GS_PLUGIN_LIBS) $(SOUP_LIBS) $(SQLITE_LIBS)
libgs_plugin_fedora_tagger_ratings_la_LDFLAGS = -module -avoid-version
libgs_plugin_fedora_tagger_ratings_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_fedora_tagger_usage_la_SOURCES = gs-plugin-fedora-tagger-usage.c
libgs_plugin_fedora_tagger_usage_la_LIBADD = $(GS_PLUGIN_LIBS) $(SOUP_LIBS) $(SQLITE_LIBS)
libgs_plugin_fedora_tagger_usage_la_LDFLAGS = -module -avoid-version
libgs_plugin_fedora_tagger_usage_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_epiphany_la_SOURCES = gs-plugin-epiphany.c
libgs_plugin_epiphany_la_LIBADD = $(GS_PLUGIN_LIBS) $(SOUP_LIBS)
libgs_plugin_epiphany_la_LDFLAGS = -module -avoid-version
libgs_plugin_epiphany_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_self_test_la_SOURCES = gs-plugin-self-test.c
libgs_plugin_self_test_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_self_test_la_LDFLAGS = -module -avoid-version
libgs_plugin_self_test_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_appstream_la_SOURCES =			\
	gs-plugin-appstream.c				\
	appstream-app.c					\
	appstream-app.h					\
	appstream-image.c				\
	appstream-image.h				\
	appstream-screenshot.c				\
	appstream-screenshot.h				\
	appstream-common.c				\
	appstream-common.h				\
	appstream-markup.c				\
	appstream-markup.h				\
	appstream-cache.c				\
	appstream-cache.h
libgs_plugin_appstream_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_appstream_la_LDFLAGS = -module -avoid-version
libgs_plugin_appstream_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_appdata_la_SOURCES =			\
	appstream-common.c				\
	appstream-common.h				\
	appstream-markup.c				\
	appstream-markup.h				\
	gs-plugin-appdata.c
libgs_plugin_appdata_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_appdata_la_LDFLAGS = -module -avoid-version
libgs_plugin_appdata_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_moduleset_gnome_la_SOURCES =		\
	gs-moduleset.c					\
	gs-moduleset.h					\
	gs-plugin-moduleset-gnome.c
libgs_plugin_moduleset_gnome_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_moduleset_gnome_la_LDFLAGS = -module -avoid-version
libgs_plugin_moduleset_gnome_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_hardcoded_popular_la_SOURCES = gs-plugin-hardcoded-popular.c
libgs_plugin_hardcoded_popular_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_hardcoded_popular_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_popular_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_hardcoded_categories_la_SOURCES = gs-plugin-hardcoded-categories.c
libgs_plugin_hardcoded_categories_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_hardcoded_categories_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_categories_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_menu_spec_categories_la_SOURCES =		\
	gs-plugin-menu-spec-categories.c		\
	menu-spec-common.c				\
	menu-spec-common.h
libgs_plugin_menu_spec_categories_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_menu_spec_categories_la_LDFLAGS = -module -avoid-version
libgs_plugin_menu_spec_categories_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_menu_spec_refine_la_SOURCES =		\
	gs-plugin-menu-spec-refine.c			\
	menu-spec-common.c				\
	menu-spec-common.h
libgs_plugin_menu_spec_refine_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_menu_spec_refine_la_LDFLAGS = -module -avoid-version
libgs_plugin_menu_spec_refine_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_hardcoded_featured_la_SOURCES = gs-plugin-hardcoded-featured.c
libgs_plugin_hardcoded_featured_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_hardcoded_featured_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_featured_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_local_ratings_la_SOURCES = gs-plugin-local-ratings.c
libgs_plugin_local_ratings_la_LIBADD = $(GS_PLUGIN_LIBS) $(SQLITE_LIBS)
libgs_plugin_local_ratings_la_LDFLAGS = -module -avoid-version
libgs_plugin_local_ratings_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_packagekit_la_SOURCES =			\
	gs-plugin-packagekit.c				\
	packagekit-common.c				\
	packagekit-common.h
libgs_plugin_packagekit_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_packagekit_refine_la_SOURCES =		\
	gs-plugin-packagekit-refine.c			\
	packagekit-common.c				\
	packagekit-common.h
libgs_plugin_packagekit_refine_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_refine_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_refine_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_packagekit_refresh_la_SOURCES =		\
	gs-plugin-packagekit-refresh.c			\
	packagekit-common.c				\
	packagekit-common.h
libgs_plugin_packagekit_refresh_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_refresh_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_refresh_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_packagekit_search_la_SOURCES =		\
	gs-plugin-packagekit-search.c			\
	packagekit-common.c				\
	packagekit-common.h
libgs_plugin_packagekit_search_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_search_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_search_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_packagekit_updates_la_SOURCES =		\
	gs-plugin-packagekit-updates.c			\
	packagekit-common.c				\
	packagekit-common.h
libgs_plugin_packagekit_updates_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_updates_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_updates_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_systemd_updates_la_SOURCES = gs-plugin-systemd-updates.c
libgs_plugin_systemd_updates_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_systemd_updates_la_LDFLAGS = -module -avoid-version
libgs_plugin_systemd_updates_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_packagekit_history_la_SOURCES = gs-plugin-packagekit-history.c
libgs_plugin_packagekit_history_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_packagekit_history_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_history_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libgs_plugin_packagekit_offline_la_SOURCES = gs-plugin-packagekit-offline.c
libgs_plugin_packagekit_offline_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_packagekit_offline_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_offline_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

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

check_PROGRAMS =					\
	gs-self-test

gs_self_test_SOURCES =					\
	appstream-common.c				\
	appstream-markup.c				\
	gs-moduleset.c					\
	gs-self-test.c

gs_self_test_LDADD =					\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)

gs_self_test_CFLAGS = $(WARNINGFLAGS_C)

TESTS = gs-self-test

EXTRA_DIST = moduleset-test.xml

-include $(top_srcdir)/git.mk
