plugindir = $(GS_PLUGIN_DIR)
plugin_LTLIBRARIES = libgs_plugin_steam.la

libgs_plugin_steam_la_SOURCES = gs-plugin-steam.c
libgs_plugin_steam_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_steam_la_LDFLAGS = -module -avoid-version
libgs_plugin_steam_la_CFLAGS = $(GS_PLUGIN_CFLAGS)

%.metainfo.xml: %.metainfo.xml.in
	$(AM_V_GEN) msgfmt --xml -d $(top_srcdir)/po --template $< -o $@

appdata_in_files = org.gnome.Software.Plugin.Steam.metainfo.xml.in
appdatadir = $(datadir)/appdata
appdata_DATA = $(appdata_in_files:.xml.in=.xml)
EXTRA_DIST = $(appdata_in_files)
CLEANFILES = $(appdata_DATA)

-include $(top_srcdir)/git.mk
