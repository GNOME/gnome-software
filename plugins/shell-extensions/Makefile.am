AM_CPPFLAGS =						\
	-DG_LOG_DOMAIN=\"GsPluginShellExtensions\"

plugindir = $(GS_PLUGIN_DIR)
plugin_LTLIBRARIES = libgs_plugin_shell-extensions.la
libgs_plugin_shell_extensions_la_SOURCES = gs-plugin-shell-extensions.c
libgs_plugin_shell_extensions_la_LIBADD = $(GS_PLUGIN_LIBS) $(JSON_GLIB_LIBS)
libgs_plugin_shell_extensions_la_LDFLAGS = -module -avoid-version
libgs_plugin_shell_extensions_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(JSON_GLIB_CFLAGS)

-include $(top_srcdir)/git.mk
