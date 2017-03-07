plugindir = $(GS_PLUGIN_DIR)
plugin_LTLIBRARIES = libgs_plugin_ubuntuone.la

libgs_plugin_ubuntuone_la_SOURCES = gs-plugin-ubuntuone.c
libgs_plugin_ubuntuone_la_LIBADD =			\
	$(GS_PLUGIN_LIBS)				\
	$(JSON_GLIB_LIBS)
libgs_plugin_ubuntuone_la_LDFLAGS = -module -avoid-version
libgs_plugin_ubuntuone_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(JSON_GLIB_CFLAGS)

-include $(top_srcdir)/git.mk
