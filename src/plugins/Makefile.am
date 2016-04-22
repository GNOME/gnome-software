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
	$(JSON_GLIB_CFLAGS)				\
	$(LIMBA_CFLAGS)					\
	$(OSTREE_CFLAGS)				\
	$(XDG_APP_CFLAGS)				\
	$(RPM_CFLAGS)					\
	-DBINDIR=\"$(bindir)\"				\
	-DDATADIR=\"$(datadir)\"			\
	-DG_LOG_DOMAIN=\"GsPlugin\"			\
	-DLIBDIR=\""$(libdir)"\"			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\"		\
	-DSBINDIR=\"$(sbindir)\"			\
	-DSYSCONFDIR=\""$(sysconfdir)"\"		\
	-DTESTDATADIR=\""$(top_srcdir)/data/tests"\"	\
	-I$(top_srcdir)/src

plugindir = $(libdir)/gs-plugins-${GS_PLUGIN_API_VERSION}
plugin_LTLIBRARIES =					\
	libgs_plugin_appstream.la			\
	libgs_plugin_dummy.la				\
	libgs_plugin_dpkg.la				\
	libgs_plugin_hardcoded-blacklist.la		\
	libgs_plugin_menu-spec-categories.la		\
	libgs_plugin_menu-spec-refine.la		\
	libgs_plugin_fedora_distro_upgrades.la		\
	libgs_plugin_provenance.la			\
	libgs_plugin_fedora_tagger_usage.la		\
	libgs_plugin_epiphany.la			\
	libgs_plugin_icons.la

if HAVE_PACKAGEKIT
plugin_LTLIBRARIES +=					\
	libgs_plugin_systemd-updates.la			\
	libgs_plugin_packagekit-refine.la		\
	libgs_plugin_packagekit-refresh.la		\
	libgs_plugin_packagekit-offline.la		\
	libgs_plugin_packagekit-origin.la		\
	libgs_plugin_packagekit-proxy.la		\
	libgs_plugin_packagekit-history.la		\
	libgs_plugin_packagekit-upgrade.la		\
	libgs_plugin_packagekit.la
endif

if HAVE_UBUNTU_REVIEWS
plugin_LTLIBRARIES += libgs_plugin_ubuntu-reviews.la
endif

if HAVE_SHELL_EXTENSIONS
plugin_LTLIBRARIES += libgs_plugin_shell-extensions.la
endif

if HAVE_FIRMWARE
plugin_LTLIBRARIES += libgs_plugin_fwupd.la
endif

if HAVE_XDG_APP
plugin_LTLIBRARIES += libgs_plugin_xdg-app.la
endif

if HAVE_OSTREE
plugin_LTLIBRARIES += libgs_plugin_ostree.la
endif

if HAVE_LIMBA
plugin_LTLIBRARIES += libgs_plugin_limba.la
endif

if HAVE_STEAM
plugin_LTLIBRARIES += libgs_plugin_steam.la
endif

if HAVE_ODRS
plugin_LTLIBRARIES += libgs_plugin_odrs.la
endif

if HAVE_RPM
plugin_LTLIBRARIES += libgs_plugin_rpm.la
endif

libgs_plugin_dummy_la_SOURCES = gs-plugin-dummy.c
libgs_plugin_dummy_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_dummy_la_LDFLAGS = -module -avoid-version
libgs_plugin_dummy_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_dpkg_la_SOURCES = gs-plugin-dpkg.c
libgs_plugin_dpkg_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_dpkg_la_LDFLAGS = -module -avoid-version
libgs_plugin_dpkg_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

if HAVE_SHELL_EXTENSIONS
libgs_plugin_shell_extensions_la_SOURCES = gs-plugin-shell-extensions.c
libgs_plugin_shell_extensions_la_LIBADD = $(GS_PLUGIN_LIBS) $(JSON_GLIB_LIBS)
libgs_plugin_shell_extensions_la_LDFLAGS = -module -avoid-version
libgs_plugin_shell_extensions_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)
endif

libgs_plugin_fedora_distro_upgrades_la_SOURCES = gs-plugin-fedora-distro-upgrades.c
libgs_plugin_fedora_distro_upgrades_la_LIBADD = $(GS_PLUGIN_LIBS) $(JSON_GLIB_LIBS) $(SOUP_LIBS)
libgs_plugin_fedora_distro_upgrades_la_LDFLAGS = -module -avoid-version
libgs_plugin_fedora_distro_upgrades_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(JSON_GLIB_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_provenance_la_SOURCES = gs-plugin-provenance.c
libgs_plugin_provenance_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_provenance_la_LDFLAGS = -module -avoid-version
libgs_plugin_provenance_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

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

libgs_plugin_appstream_la_SOURCES =			\
	gs-appstream.c					\
	gs-appstream.h					\
	gs-plugin-appstream.c
libgs_plugin_appstream_la_LIBADD = $(GS_PLUGIN_LIBS) $(APPSTREAM_LIBS)
libgs_plugin_appstream_la_LDFLAGS = -module -avoid-version
libgs_plugin_appstream_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

if HAVE_LIMBA
libgs_plugin_limba_la_SOURCES = gs-plugin-limba.c
libgs_plugin_limba_la_LIBADD = $(GS_PLUGIN_LIBS) $(LIMBA_LIBS)
libgs_plugin_limba_la_LDFLAGS = -module -avoid-version
libgs_plugin_limba_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)
endif

if HAVE_XDG_APP
libgs_plugin_xdg_app_la_SOURCES =			\
	gs-appstream.c					\
	gs-appstream.h					\
	gs-plugin-xdg-app.c
libgs_plugin_xdg_app_la_LIBADD = $(GS_PLUGIN_LIBS) $(XDG_APP_LIBS)
libgs_plugin_xdg_app_la_LDFLAGS = -module -avoid-version
libgs_plugin_xdg_app_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)
endif

if HAVE_OSTREE
libgs_plugin_ostree_la_SOURCES = gs-plugin-ostree.c
libgs_plugin_ostree_la_LIBADD = $(GS_PLUGIN_LIBS) $(OSTREE_LIBS)
libgs_plugin_ostree_la_LDFLAGS = -module -avoid-version
libgs_plugin_ostree_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)
endif

if HAVE_ODRS
libgs_plugin_odrs_la_SOURCES = gs-plugin-odrs.c
libgs_plugin_odrs_la_LIBADD = $(GS_PLUGIN_LIBS) $(JSON_GLIB_LIBS)
libgs_plugin_odrs_la_LDFLAGS = -module -avoid-version
libgs_plugin_odrs_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)
endif

if HAVE_RPM
libgs_plugin_rpm_la_SOURCES = gs-plugin-rpm.c
libgs_plugin_rpm_la_LIBADD = $(GS_PLUGIN_LIBS) $(RPM_LIBS)
libgs_plugin_rpm_la_LDFLAGS = -module -avoid-version
libgs_plugin_rpm_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)
endif

if HAVE_STEAM
libgs_plugin_steam_la_SOURCES = gs-plugin-steam.c
libgs_plugin_steam_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_steam_la_LDFLAGS = -module -avoid-version
libgs_plugin_steam_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)
endif

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

libgs_plugin_hardcoded_blacklist_la_SOURCES = gs-plugin-hardcoded-blacklist.c
libgs_plugin_hardcoded_blacklist_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_hardcoded_blacklist_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_blacklist_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

if HAVE_UBUNTU_REVIEWS
libgs_plugin_ubuntu_reviews_la_SOURCES =		\
	gs-plugin-ubuntu-reviews.c
libgs_plugin_ubuntu_reviews_la_LIBADD = $(GS_PLUGIN_LIBS) $(SOUP_LIBS) $(JSON_GLIB_LIBS) $(SQLITE_LIBS)
libgs_plugin_ubuntu_reviews_la_LDFLAGS = -module -avoid-version
libgs_plugin_ubuntu_reviews_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)
endif

libgs_plugin_packagekit_la_SOURCES =			\
	gs-plugin-packagekit.c				\
	packagekit-common.c				\
	packagekit-common.h
libgs_plugin_packagekit_la_LIBADD = $(GS_PLUGIN_LIBS) $(PACKAGEKIT_LIBS)
libgs_plugin_packagekit_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_packagekit_refine_la_SOURCES =		\
	gs-plugin-packagekit-refine.c			\
	gs-markdown.c					\
	gs-markdown.h					\
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

libgs_plugin_packagekit_upgrade_la_SOURCES =		\
	gs-plugin-packagekit-upgrade.c			\
	packagekit-common.c				\
	packagekit-common.h
libgs_plugin_packagekit_upgrade_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_packagekit_upgrade_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_upgrade_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_packagekit_offline_la_SOURCES = gs-plugin-packagekit-offline.c
libgs_plugin_packagekit_offline_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_packagekit_offline_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_offline_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_packagekit_origin_la_SOURCES = gs-plugin-packagekit-origin.c
libgs_plugin_packagekit_origin_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_packagekit_origin_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_origin_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

libgs_plugin_packagekit_proxy_la_SOURCES = gs-plugin-packagekit-proxy.c
libgs_plugin_packagekit_proxy_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_packagekit_proxy_la_LDFLAGS = -module -avoid-version
libgs_plugin_packagekit_proxy_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(WARN_CFLAGS)

if ENABLE_TESTS
check_PROGRAMS =						\
	gs-self-test

gs_self_test_SOURCES =						\
	gs-markdown.c						\
	gs-self-test.c

gs_self_test_LDADD =						\
	$(GLIB_LIBS)						\
	$(GTK_LIBS)

gs_self_test_CFLAGS = $(WARN_CFLAGS)

TESTS = gs-self-test
endif

-include $(top_srcdir)/git.mk
