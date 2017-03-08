AM_CPPFLAGS =						\
	-DG_LOG_DOMAIN=\"GsPlugin\"

plugindir = $(GS_PLUGIN_DIR)
plugin_LTLIBRARIES =					\
	libgs_plugin_appstream.la			\
	libgs_plugin_key-colors.la			\
	libgs_plugin_desktop-categories.la		\
	libgs_plugin_desktop-menu-path.la		\
	libgs_plugin_generic-updates.la			\
	libgs_plugin_hardcoded-blacklist.la		\
	libgs_plugin_hardcoded-popular.la		\
	libgs_plugin_hardcoded-featured.la		\
	libgs_plugin_provenance.la			\
	libgs_plugin_provenance-license.la		\
	libgs_plugin_icons.la

libgs_plugin_generic_updates_la_SOURCES = gs-plugin-generic-updates.c
libgs_plugin_generic_updates_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_generic_updates_la_LDFLAGS = -module -avoid-version
libgs_plugin_generic_updates_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

libgs_plugin_key_colors_la_SOURCES = gs-plugin-key-colors.c
libgs_plugin_key_colors_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_key_colors_la_LDFLAGS = -module -avoid-version
libgs_plugin_key_colors_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

libgs_plugin_provenance_la_SOURCES = gs-plugin-provenance.c
libgs_plugin_provenance_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_provenance_la_LDFLAGS = -module -avoid-version
libgs_plugin_provenance_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

libgs_plugin_provenance_license_la_SOURCES = gs-plugin-provenance-license.c
libgs_plugin_provenance_license_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_provenance_license_la_LDFLAGS = -module -avoid-version
libgs_plugin_provenance_license_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

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

-include $(top_srcdir)/git.mk
