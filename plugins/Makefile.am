## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

# core
SUBDIRS =

# non-core, but no configure option required
SUBDIRS += dpkg
SUBDIRS += ubuntuone

if HAVE_LIMBA
SUBDIRS += limba
endif

if HAVE_ODRS
SUBDIRS += odrs
endif

if HAVE_STEAM
SUBDIRS += steam
endif

if HAVE_UBUNTU_REVIEWS
SUBDIRS += ubuntu-reviews
endif

if HAVE_OSTREE
SUBDIRS += ostree
endif

if HAVE_EXTERNAL_APPSTREAM
SUBDIRS += external-appstream
endif

if HAVE_SNAP
SUBDIRS += snap
endif

if HAVE_FIRMWARE
SUBDIRS += fwupd
endif

if HAVE_PACKAGEKIT
SUBDIRS += packagekit
endif

AM_CPPFLAGS =						\
	-DDATADIR=\"$(datadir)\"			\
	-DG_LOG_DOMAIN=\"GsPlugin\"			\
	-DLIBEXECDIR=\""$(libexecdir)"\"		\
	-DLOCALSTATEDIR=\""$(localstatedir)"\"		\
	-DLOCALEDIR=\""$(localedir)"\"			\
	-DTESTDATADIR=\""$(top_srcdir)/data"\"		\
	-DLOCALPLUGINDIR=\""$(top_builddir)/src/plugins/.libs"\"

plugindir = $(GS_PLUGIN_DIR)
plugin_LTLIBRARIES =					\
	libgs_plugin_appstream.la			\
	libgs_plugin_key-colors.la			\
	libgs_plugin_desktop-categories.la		\
	libgs_plugin_desktop-menu-path.la		\
	libgs_plugin_dummy.la				\
	libgs_plugin_generic-updates.la			\
	libgs_plugin_hardcoded-blacklist.la		\
	libgs_plugin_hardcoded-popular.la		\
	libgs_plugin_hardcoded-featured.la		\
	libgs_plugin_fedora-distro-upgrades.la		\
	libgs_plugin_provenance.la			\
	libgs_plugin_provenance-license.la		\
	libgs_plugin_repos.la				\
	libgs_plugin_fedora-tagger-usage.la		\
	libgs_plugin_icons.la

appdata_in_files =

if HAVE_WEBAPPS
plugin_LTLIBRARIES += libgs_plugin_epiphany.la
endif

if HAVE_GUDEV
plugin_LTLIBRARIES += libgs_plugin_modalias.la
endif

if HAVE_SHELL_EXTENSIONS
plugin_LTLIBRARIES += libgs_plugin_shell-extensions.la
endif

if HAVE_FLATPAK
plugin_LTLIBRARIES += libgs_plugin_flatpak.la
endif

if HAVE_RPM
plugin_LTLIBRARIES += libgs_plugin_rpm.la
endif

libgs_plugin_dummy_la_SOURCES = gs-plugin-dummy.c
libgs_plugin_dummy_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_dummy_la_LDFLAGS = -module -avoid-version
libgs_plugin_dummy_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

libgs_plugin_generic_updates_la_SOURCES = gs-plugin-generic-updates.c
libgs_plugin_generic_updates_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_generic_updates_la_LDFLAGS = -module -avoid-version
libgs_plugin_generic_updates_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

libgs_plugin_key_colors_la_SOURCES = gs-plugin-key-colors.c
libgs_plugin_key_colors_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_key_colors_la_LDFLAGS = -module -avoid-version
libgs_plugin_key_colors_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

if HAVE_SHELL_EXTENSIONS
libgs_plugin_shell_extensions_la_SOURCES = gs-plugin-shell-extensions.c
libgs_plugin_shell_extensions_la_LIBADD = $(GS_PLUGIN_LIBS) $(JSON_GLIB_LIBS)
libgs_plugin_shell_extensions_la_LDFLAGS = -module -avoid-version
libgs_plugin_shell_extensions_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(JSON_GLIB_CFLAGS)
endif

libgs_plugin_fedora_distro_upgrades_la_SOURCES = gs-plugin-fedora-distro-upgrades.c
libgs_plugin_fedora_distro_upgrades_la_LIBADD = $(GS_PLUGIN_LIBS) $(JSON_GLIB_LIBS)
libgs_plugin_fedora_distro_upgrades_la_LDFLAGS = -module -avoid-version
libgs_plugin_fedora_distro_upgrades_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(JSON_GLIB_CFLAGS) $(WARN_CFLAGS)

if HAVE_GUDEV
libgs_plugin_modalias_la_SOURCES = gs-plugin-modalias.c
libgs_plugin_modalias_la_LIBADD = $(GS_PLUGIN_LIBS) $(GUDEV_LIBS)
libgs_plugin_modalias_la_LDFLAGS = -module -avoid-version
libgs_plugin_modalias_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(GUDEV_CFLAGS)
endif

libgs_plugin_provenance_la_SOURCES = gs-plugin-provenance.c
libgs_plugin_provenance_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_provenance_la_LDFLAGS = -module -avoid-version
libgs_plugin_provenance_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

libgs_plugin_provenance_license_la_SOURCES = gs-plugin-provenance-license.c
libgs_plugin_provenance_license_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_provenance_license_la_LDFLAGS = -module -avoid-version
libgs_plugin_provenance_license_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

libgs_plugin_fedora_tagger_usage_la_SOURCES = gs-plugin-fedora-tagger-usage.c
libgs_plugin_fedora_tagger_usage_la_LIBADD = $(GS_PLUGIN_LIBS) $(SQLITE_LIBS)
libgs_plugin_fedora_tagger_usage_la_LDFLAGS = -module -avoid-version
libgs_plugin_fedora_tagger_usage_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(SQLITE_CFLAGS)

appdata_in_files += org.gnome.Software.Plugin.Epiphany.metainfo.xml.in
libgs_plugin_epiphany_la_SOURCES = gs-plugin-epiphany.c
libgs_plugin_epiphany_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_epiphany_la_LDFLAGS = -module -avoid-version
libgs_plugin_epiphany_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

libgs_plugin_icons_la_SOURCES = gs-plugin-icons.c
libgs_plugin_icons_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_icons_la_LDFLAGS = -module -avoid-version
libgs_plugin_icons_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

libgs_plugin_appstream_la_SOURCES =			\
	gs-appstream.c					\
	gs-appstream.h					\
	gs-plugin-appstream.c
libgs_plugin_appstream_la_LIBADD = $(GS_PLUGIN_LIBS) $(APPSTREAM_LIBS)
libgs_plugin_appstream_la_LDFLAGS = -module -avoid-version
libgs_plugin_appstream_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

if HAVE_FLATPAK
appdata_in_files += org.gnome.Software.Plugin.Flatpak.metainfo.xml.in
libgs_plugin_flatpak_la_SOURCES =			\
	gs-appstream.c					\
	gs-appstream.h					\
	gs-flatpak.c					\
	gs-flatpak.h					\
	gs-flatpak-symlinks.c				\
	gs-flatpak-symlinks.h				\
	gs-plugin-flatpak.c
libgs_plugin_flatpak_la_LIBADD = $(GS_PLUGIN_LIBS) $(FLATPAK_LIBS)
libgs_plugin_flatpak_la_LDFLAGS = -module -avoid-version
libgs_plugin_flatpak_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(FLATPAK_CFLAGS)
endif

if HAVE_RPM
libgs_plugin_rpm_la_SOURCES = gs-plugin-rpm.c
libgs_plugin_rpm_la_LIBADD = $(GS_PLUGIN_LIBS) $(RPM_LIBS)
libgs_plugin_rpm_la_LDFLAGS = -module -avoid-version
libgs_plugin_rpm_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(RPM_CFLAGS)
endif

libgs_plugin_repos_la_SOURCES = gs-plugin-repos.c
libgs_plugin_repos_la_LIBADD = $(GS_PLUGIN_LIBS) $(RPM_LIBS)
libgs_plugin_repos_la_LDFLAGS = -module -avoid-version
libgs_plugin_repos_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

libgs_plugin_desktop_categories_la_SOURCES =		\
	gs-plugin-desktop-categories.c			\
	gs-desktop-common.c				\
	gs-desktop-common.h
libgs_plugin_desktop_categories_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_desktop_categories_la_LDFLAGS = -module -avoid-version
libgs_plugin_desktop_categories_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

libgs_plugin_desktop_menu_path_la_SOURCES =		\
	gs-plugin-desktop-menu-path.c			\
	gs-desktop-common.c				\
	gs-desktop-common.h
libgs_plugin_desktop_menu_path_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_desktop_menu_path_la_LDFLAGS = -module -avoid-version
libgs_plugin_desktop_menu_path_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

libgs_plugin_hardcoded_blacklist_la_SOURCES = gs-plugin-hardcoded-blacklist.c
libgs_plugin_hardcoded_blacklist_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_hardcoded_blacklist_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_blacklist_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

libgs_plugin_hardcoded_popular_la_SOURCES = gs-plugin-hardcoded-popular.c
libgs_plugin_hardcoded_popular_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_hardcoded_popular_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_popular_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

libgs_plugin_hardcoded_featured_la_SOURCES = gs-plugin-hardcoded-featured.c
libgs_plugin_hardcoded_featured_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_hardcoded_featured_la_LDFLAGS = -module -avoid-version
libgs_plugin_hardcoded_featured_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

# MetaInfo files
%.metainfo.xml: %.metainfo.xml.in
	$(AM_V_GEN) msgfmt --xml -d $(top_srcdir)/po --template $< -o $@

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
