plugindir = $(GS_PLUGIN_DIR)
plugin_LTLIBRARIES = libgs_plugin_fedora-distro-upgrades.la

libgs_plugin_fedora_distro_upgrades_la_SOURCES = gs-plugin-fedora-distro-upgrades.c
libgs_plugin_fedora_distro_upgrades_la_LIBADD = $(GS_PLUGIN_LIBS) $(JSON_GLIB_LIBS)
libgs_plugin_fedora_distro_upgrades_la_LDFLAGS = -module -avoid-version
libgs_plugin_fedora_distro_upgrades_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(JSON_GLIB_CFLAGS) $(WARN_CFLAGS)

-include $(top_srcdir)/git.mk
