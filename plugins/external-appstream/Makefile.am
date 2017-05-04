AM_CPPFLAGS =						\
	-DG_LOG_DOMAIN=\"GsPluginExternalAppstream\"	\
	-DLIBEXECDIR=\"$(libexecdir)\"			\
	-DLOCALEDIR=\""$(localedir)"\"			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\"

plugindir = $(GS_PLUGIN_DIR)
plugin_LTLIBRARIES = libgs_plugin_external-appstream.la

libgs_plugin_external_appstream_la_SOURCES = gs-plugin-external-appstream.c
libgs_plugin_external_appstream_la_LIBADD = $(GS_PLUGIN_LIBS)
libgs_plugin_external_appstream_la_LDFLAGS = -module -avoid-version
libgs_plugin_external_appstream_la_CFLAGS = $(GS_PLUGIN_CFLAGS) $(SOUP_CFLAGS)

libexec_PROGRAMS = gnome-software-install-appstream
gnome_software_install_appstream_SOURCES = gs-install-appstream.c
gnome_software_install_appstream_LDADD = $(GLIB_LIBS) $(APPSTREAM_LIBS)
gnome_software_install_appstream_CFLAGS = $(APPSTREAM_CFLAGS) $(PIE_CFLAGS) $(WARN_CFLAGS)
gnome_software_install_appstream_LDFLAGS = $(PIE_LDFLAGS) $(RELRO_LDFLAGS)

-include $(top_srcdir)/git.mk
