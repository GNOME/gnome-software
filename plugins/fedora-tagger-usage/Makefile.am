AM_CPPFLAGS =						\
	-DG_LOG_DOMAIN=\"GsPluginFedoraTaggerUsage\"

plugindir = $(GS_PLUGIN_DIR)
plugin_LTLIBRARIES = libgs_plugin_fedora-tagger-usage.la

libgs_plugin_fedora_tagger_usage_la_SOURCES = gs-plugin-fedora-tagger-usage.c
libgs_plugin_fedora_tagger_usage_la_LIBADD = $(GS_PLUGIN_LIBS) $(SQLITE_LIBS)
libgs_plugin_fedora_tagger_usage_la_LDFLAGS = -module -avoid-version
libgs_plugin_fedora_tagger_usage_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(SQLITE_CFLAGS)

-include $(top_srcdir)/git.mk
