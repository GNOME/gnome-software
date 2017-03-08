AM_CPPFLAGS =						\
	-DG_LOG_DOMAIN=\"GsPluginOstree\"

plugindir = $(GS_PLUGIN_DIR)
plugin_LTLIBRARIES = libgs_plugin_ostree.la

libgs_plugin_ostree_la_SOURCES = gs-plugin-ostree.c
libgs_plugin_ostree_la_LIBADD = $(GS_PLUGIN_LIBS) $(OSTREE_LIBS)
libgs_plugin_ostree_la_LDFLAGS = -module -avoid-version
libgs_plugin_ostree_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(OSTREE_CFLAGS)

plugin_LTLIBRARIES += libgs_plugin_rpm.la
libgs_plugin_rpm_la_SOURCES = gs-plugin-rpm.c
libgs_plugin_rpm_la_LIBADD = $(GS_PLUGIN_LIBS) $(RPM_LIBS)
libgs_plugin_rpm_la_LDFLAGS = -module -avoid-version
libgs_plugin_rpm_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(RPM_CFLAGS)

-include $(top_srcdir)/git.mk
