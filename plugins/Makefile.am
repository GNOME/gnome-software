## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

# core
SUBDIRS = core

# non-core, but no configure option required
SUBDIRS += dpkg
SUBDIRS += dummy
SUBDIRS += fedora-tagger-usage
SUBDIRS += ubuntuone
SUBDIRS += repos

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

if HAVE_GUDEV
SUBDIRS += modalias
endif

if HAVE_WEBAPPS
SUBDIRS += epiphany
endif

AM_CPPFLAGS =						\
	-DDATADIR=\"$(datadir)\"			\
	-DG_LOG_DOMAIN=\"GsPlugin\"			\
	-DLIBEXECDIR=\""$(libexecdir)"\"		\
	-DLOCALSTATEDIR=\""$(localstatedir)"\"		\
	-DLOCALEDIR=\""$(localedir)"\"			\
	-DTESTDATADIR=\""$(top_srcdir)/data"\"		\
	-DLOCALPLUGINDIR=\""$(top_builddir)/plugins/.libs"\"	\
	-DLOCALPLUGINDIR_CORE=\""$(top_builddir)/plugins/core/.libs"\"

plugindir = $(GS_PLUGIN_DIR)
plugin_LTLIBRARIES =					\
	libgs_plugin_fedora-distro-upgrades.la

appdata_in_files =

if HAVE_SHELL_EXTENSIONS
plugin_LTLIBRARIES += libgs_plugin_shell-extensions.la
endif

if HAVE_FLATPAK
plugin_LTLIBRARIES += libgs_plugin_flatpak.la
endif

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
