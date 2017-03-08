AM_CPPFLAGS =						\
	-DG_LOG_DOMAIN=\"GsPluginUbuntuReviews\"

plugindir = $(GS_PLUGIN_DIR)
plugin_LTLIBRARIES = libgs_plugin_ubuntu-reviews.la

libgs_plugin_ubuntu_reviews_la_SOURCES = gs-plugin-ubuntu-reviews.c
libgs_plugin_ubuntu_reviews_la_LIBADD =			\
	$(GS_PLUGIN_LIBS)				\
	$(JSON_GLIB_LIBS)				\
	$(SQLITE_LIBS)					\
	$(OAUTH_LIBS)
libgs_plugin_ubuntu_reviews_la_LDFLAGS = -module -avoid-version
libgs_plugin_ubuntu_reviews_la_CFLAGS =			\
	$(GS_PLUGIN_CFLAGS)				\
	$(JSON_GLIB_CFLAGS)				\
	$(OAUTH_CFLAGS)					\
	$(SQLITE_CFLAGS)

-include $(top_srcdir)/git.mk
