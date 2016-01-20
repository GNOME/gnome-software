## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

AM_CPPFLAGS =						\
	$(APPSTREAM_CFLAGS)				\
	$(GLIB_CFLAGS)					\
	$(GTK_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	$(PACKAGEKIT_CFLAGS)				\
	$(SOUP_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	$(FWUPD_CFLAGS)					\
	$(LIMBA_CFLAGS)					\
	-DBINDIR=\"$(bindir)\"				\
	-DDATADIR=\"$(datadir)\"			\
	-DGS_MODULESETDIR=\"$(datadir)/gnome-software/modulesets.d\" \
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
	libgs_plugin_appstream.la			\
	libgs_plugin_hardcoded-featured.la		\
	libgs_plugin_hardcoded-blacklist.la		\
	libgs_plugin_moduleset.la			\
	libgs_plugin_menu-spec-categories.la		\
	libgs_plugin_menu-spec-refine.la		\
	libgs_plugin_local-ratings.la			\
	libgs_plugin_fedora_provenance.la		\
	libgs_plugin_fedora_tagger_ratings.la		\
	libgs_plugin_fedora_tagger_usage.la		\
	libgs_plugin_epiphany.la			\
	libgs_plugin_icons.la				\
	libgs_plugin_systemd-updates.la			\
	libgs_plugin_packagekit-refine.la		\
	libgs_plugin_packagekit-refresh.la		\
	libgs_plugin_packagekit-offline.la		\
	libgs_plugin_packagekit-origin.la		\
	libgs_plugin_packagekit-history.la		\
	libgs_plugin_packagekit.la

if HAVE_FIRMWARE
plugin_LTLIBRARIES += libgs_plugin_fwupd.la
endif

if HAVE_LIMBA
plugin_LTLIBRARIES += libgs_plugin_limba.la
endif

libgs_plugin_dummy_la_SOURCES = gs-plugin-dummy.c
libgs_plugin_dummy_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_dummy_la_LDFLAGS = -module -avoid-version
libgs_plugin_dummy_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_fedora_provenance_la_SOURCES = gs-plugin-fedora-provenance.c
libgs_plugin_fedora_provenance_la_LIBADD = $(GS_PLUGIN_LIBS) $(SOUP_LIBS) $(SQLITE_LIBS)
libgs_plugin_fedora_provenance_la_LDFLAGS = -module -avoid-version
libgs_plugin_fedora_provenance_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_fedora_tagger_ratings_la_SOURCES = gs-plugin-fedora-tagger-ratings.c
libgs_plugin_fedora_tagger_ratings_la_LIBADD = $(GS_PLUGIN_LIBS) $(JSON_GLIB_LIBS) $(SOUP_LIBS) $(SQLITE_LIBS)
libgs_plugin_fedora_tagger_ratings_la_LDFLAGS = -module -avoid-version
libgs_plugin_fedora_tagger_ratings_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(JSON_GLIB_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_fedora_tagger_usage_la_SOURCES = gs-plugin-fedora-tagger-usage.c
libgs_plugin_fedora_tagger_usage_la_LIBADD = $(GS_PLUGIN_LIBS) $(SOUP_LIBS) $(SQLITE_LIBS)
libgs_plugin_fedora_tagger_usage_la_LDFLAGS = -module -avoid-version
libgs_plugin_fedora_tagger_usage_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_epiphany_la_SOURCES = gs-plugin-epiphany.c
libgs_plugin_epiphany_la_LIBADD = $(GS_PLUGIN_LIBS) $(SOUP_LIBS)
libgs_plugin_epiphany_la_LDFLAGS = -module -avoid-version
libgs_plugin_epiphany_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_icons_la_SOURCES = gs-plugin-icons.c
libgs_plugin_icons_la_LIBADD = $(GS_PLUGIN_LIBS) $(SOUP_LIBS)
libgs_plugin_icons_la_LDFLAGS = -module -avoid-version
libgs_plugin_icons_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_self_test_la_SOURCES = gs-plugin-self-test.c
libgs_plugin_self_test_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_self_test_la_LDFLAGS = -module -avoid-version
libgs_plugin_self_test_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_appstream_la_SOURCES = gs-plugin-appstream.c
libgs_plugin_appstream_la_LIBADD = $(GS_PLUGIN_LIBS) $(APPSTREAM_LIBS)
libgs_plugin_appstream_la_LDFLAGS = -module -avoid-version
libgs_plugin_appstream_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

if HAVE_LIMBA
libgs_plugin_limba_la_SOURCES = gs-plugin-limba.c
libgs_plugin_limba_la_LIBADD = $(GS_PLUGIN_LIBS) $(LIMBA_LIBS)
libgs_plugin_limba_la_LDFLAGS = -module -avoid-version
libgs_plugin_limba_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)
endif

libgs_plugin_moduleset_la_SOURCES =			\
	gs-moduleset.c					\
	gs-moduleset.h					\
	gs-plugin-moduleset.c
libgs_plugin_moduleset_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_moduleset_la_LDFLAGS = -module -avoid-version
libgs_plugin_moduleset_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_menu_spec_categories_la_SOURCES =		\
	gs-plugin-menu-spec-categories.c		\
	menu-spec-common.c				\
	menu-spec-common.h
libgs_plugin_menu_spec_categories_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_menu_spec_categories_la_LDFLAGS = -module -avoid-version
libgs_plugin_menu_spec_categories_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_menu_spec_refine_la_SOURCES =		\
	gs-plugin-menu-spec-refine.c			\
	menu-spec-common.c				\
	menu-spec-common.h
libgs_plugin_menu_spec_refine_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_menu_spec_refine_la_LDFLAGS = -module -avoid-version
libgs_plugin_menu_spec_refine_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_hardcoded_featured_la_SOURCES = gs-plugin-hardcoded-featured.c
libgs_plugin_hardcoded_featured_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_hardcoded_featured_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_featured_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_hardcoded_blacklist_la_SOURCES = gs-plugin-hardcoded-blacklist.c
libgs_plugin_hardcoded_blacklist_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_hardcoded_blacklist_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_blacklist_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_local_ratings_la_SOURCES = gs-plugin-local-ratings.c
libgs_plugin_local_ratings_la_LIBADD = $(GS_PLUGIN_LIBS) $(SQLITE_LIBS)
libgs_plugin_local_ratings_la_LDFLAGS = -module -avoid-version
libgs_plugin_local_ratings_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_packagekit_la_SOURCES =			\
	gs-plugin-packagekit.c				\
	packagekit-common.c				\
	packagekit-common.h
libgs_plugin_packagekit_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_packagekit_refine_la_SOURCES =		\
	gs-plugin-packagekit-refine.c			\
	packagekit-common.c				\
	packagekit-common.h
libgs_plugin_packagekit_refine_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_refine_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_refine_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_packagekit_refresh_la_SOURCES =		\
	gs-plugin-packagekit-refresh.c			\
	packagekit-common.c				\
	packagekit-common.h
libgs_plugin_packagekit_refresh_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_refresh_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_refresh_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_systemd_updates_la_SOURCES = gs-plugin-systemd-updates.c
libgs_plugin_systemd_updates_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_systemd_updates_la_LDFLAGS = -module -avoid-version
libgs_plugin_systemd_updates_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

if HAVE_FIRMWARE
libgs_plugin_fwupd_la_SOURCES = gs-plugin-fwupd.c
libgs_plugin_fwupd_la_LIBADD = $(GS_PLUGIN_LIBS) $(FWUPD_LIBS)
libgs_plugin_fwupd_la_LDFLAGS = -module -avoid-version
libgs_plugin_fwupd_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)
endif

libgs_plugin_packagekit_history_la_SOURCES = gs-plugin-packagekit-history.c
libgs_plugin_packagekit_history_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_packagekit_history_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_history_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_packagekit_offline_la_SOURCES = gs-plugin-packagekit-offline.c
libgs_plugin_packagekit_offline_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_packagekit_offline_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_offline_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_packagekit_origin_la_SOURCES = gs-plugin-packagekit-origin.c
libgs_plugin_packagekit_origin_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_packagekit_origin_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_origin_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

check_PROGRAMS =					\
	gs-self-test

gs_self_test_SOURCES =					\
	gs-moduleset.c					\
	gs-self-test.c

gs_self_test_LDADD =					\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)

gs_self_test_CFLAGS = $(WARN_CFLAGS)

TESTS = gs-self-test

EXTRA_DIST = moduleset-test.xml

-include $(top_srcdir)/git.mk
