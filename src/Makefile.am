SUBDIRS =						\
	plugins

AM_CPPFLAGS =						\
	$(APPSTREAM_CFLAGS)				\
	$(GLIB_CFLAGS)					\
	$(GTK_CFLAGS)					\
	$(SOUP_CFLAGS)					\
	$(PACKAGEKIT_CFLAGS)				\
	$(GNOME_DESKTOP_CFLAGS)				\
	$(POLKIT_CFLAGS)				\
	-DG_LOG_DOMAIN=\"Gs\"				\
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
	-DGS_MODULESETDIR=\"$(datadir)/gnome-software/modulesets.d\" \
	-DLIBDIR=\"$(libdir)\"				\
	-DBINDIR=\"$(bindir)\"				\
	-DDATADIR=\"$(datadir)\"			\
	-DVERSION="\"$(VERSION)\""			\
	-DLOCALEDIR=\""$(localedir)"\"			\
	-DGS_DATA=\"$(pkgdatadir)\"

@INTLTOOL_DESKTOP_RULE@
desktopdir = $(datadir)/applications
desktop_in_files =					\
	gnome-software-local-file.desktop.in		\
	org.gnome.Software.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

UI_FILES =						\
	app-folder-dialog.ui				\
	menus.ui					\
	app-tile.ui					\
	category-tile.ui				\
	feature-tile.ui					\
	gnome-software.ui				\
	gs-app-addon-row.ui				\
	gs-app-row.ui					\
	gs-first-run-dialog.ui				\
	gs-history-dialog.ui				\
	gs-shell-category.ui				\
	gs-shell-extras.ui				\
	gs-shell-details.ui				\
	gs-shell-installed.ui				\
	gs-shell-overview.ui				\
	gs-shell-search.ui				\
	gs-shell-updates.ui				\
	gs-sources-dialog.ui				\
	gs-sources-dialog-row.ui			\
	gs-star-widget.ui				\
	gs-update-dialog.ui				\
	screenshot-image.ui				\
	popular-tile.ui

dbusservicedir = $(datadir)/dbus-1/services
dbusservice_DATA =					\
	org.freedesktop.PackageKit.service		\
	org.gnome.Software.service

org.freedesktop.PackageKit.service: org.freedesktop.PackageKit.service.in Makefile
	$(AM_V_GEN) sed -e "s|\@bindir\@|$(bindir)|" $<> $@

org.gnome.Software.service: org.gnome.Software.service.in Makefile
	$(AM_V_GEN) sed -e "s|\@bindir\@|$(bindir)|" $<> $@

searchproviderdir = $(datadir)/gnome-shell/search-providers
searchprovider_DATA = gnome-software-search-provider.ini

autostartdir = $(sysconfdir)/xdg/autostart
autostart_DATA = gnome-software-service.desktop

gnome-software-service.desktop: gnome-software-service.desktop.in Makefile
	$(AM_V_GEN) sed -e "s|\@bindir\@|$(bindir)|" $<> $@

noinst_PROGRAMS =					\
	gnome-software-cmd

gnome_software_cmd_SOURCES =				\
	gs-app.c					\
	gs-cmd.c					\
	gs-utils.c					\
	gs-plugin-loader.c				\
	gs-plugin-loader-sync.c				\
	gs-category.c					\
	gs-plugin.c

gnome_software_cmd_LDADD =				\
	$(APPSTREAM_LIBS)				\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)

gnome_software_cmd_CFLAGS =				\
	$(WARN_CFLAGS)

bin_PROGRAMS =						\
	gnome-software

gnome_software_SOURCES =				\
	$(packagekit_built_sources)			\
	$(packagekit_modify2_built_sources)		\
	gs-resources.c					\
	gs-resources.h					\
	gs-dbus-helper.c				\
	gs-dbus-helper.h				\
	gs-utils.c					\
	gs-utils.h					\
	gs-app.c					\
	gs-app.h					\
	gs-category.c					\
	gs-category.h					\
	gs-app-addon-row.c				\
	gs-app-addon-row.h				\
	gs-app-row.c					\
	gs-app-row.h					\
	gs-star-widget.c				\
	gs-star-widget.h				\
	gs-popular-tile.c				\
	gs-popular-tile.h				\
	gs-feature-tile.c				\
	gs-feature-tile.h				\
	gs-category-tile.c				\
	gs-category-tile.h				\
	gs-app-tile.c					\
	gs-app-tile.h					\
	gs-app-folder-dialog.c				\
	gs-app-folder-dialog.h				\
	gs-first-run-dialog.c				\
	gs-first-run-dialog.h				\
	gs-history-dialog.c				\
	gs-history-dialog.h				\
	gs-box.h					\
	gs-box.c					\
	gs-fixed-size-bin.h				\
	gs-fixed-size-bin.c				\
	gs-hiding-box.h					\
	gs-hiding-box.c					\
	gs-language.c					\
	gs-language.h					\
	gs-page.c					\
	gs-page.h					\
	gs-plugin.c					\
	gs-plugin.h					\
	gs-progress-button.c				\
	gs-progress-button.h				\
	gs-screenshot-image.c				\
	gs-screenshot-image.h				\
	gs-shell.c					\
	gs-shell.h					\
	gs-markdown.c					\
	gs-markdown.h					\
	gs-shell-details.c				\
	gs-shell-details.h				\
	gs-shell-category.c				\
	gs-shell-category.h				\
	gs-shell-extras.c				\
	gs-shell-extras.h				\
	gs-shell-installed.c				\
	gs-shell-installed.h				\
	gs-shell-overview.c				\
	gs-shell-overview.h				\
	gs-shell-updates.c				\
	gs-shell-updates.h				\
	gs-shell-search.c				\
	gs-shell-search.h				\
	gs-sources-dialog.c				\
	gs-sources-dialog.h				\
	gs-sources-dialog-row.c				\
	gs-sources-dialog-row.h				\
	gs-update-dialog.c				\
	gs-update-dialog.h				\
	gs-update-list.c				\
	gs-update-list.h				\
	gs-update-monitor.c				\
	gs-update-monitor.h				\
	gs-vendor.c					\
	gs-vendor.h					\
	gs-proxy-settings.c				\
	gs-proxy-settings.h				\
	gs-offline-updates.c				\
	gs-offline-updates.h				\
	gs-plugin-loader.c				\
	gs-plugin-loader.h				\
	gs-plugin-loader-sync.c				\
	gs-plugin-loader-sync.h				\
	gs-shell-search-provider-generated.c		\
	gs-shell-search-provider-generated.h		\
	gs-shell-search-provider.c			\
	gs-shell-search-provider.h			\
	gs-application.c				\
	gs-application.h				\
	gs-folders.c					\
	gs-folders.h					\
	gs-main.c

gnome_software_LDADD =					\
	$(APPSTREAM_LIBS)				\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)					\
	$(SOUP_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(GNOME_DESKTOP_LIBS)				\
	$(POLKIT_LIBS)					\
	-lm

gnome_software_CFLAGS =					\
	-DLIBEXECDIR=\"$(libexecdir)\"			\
	$(WARN_CFLAGS)

packagekit_built_sources = gs-packagekit-generated.c gs-packagekit-generated.h
$(packagekit_built_sources): Makefile.am org.freedesktop.PackageKit.xml
	$(AM_V_GEN) gdbus-codegen				\
		--interface-prefix org.freedesktop.		\
		--c-namespace Gs				\
		--generate-c-code gs-packagekit-generated	\
		$(srcdir)/org.freedesktop.PackageKit.xml

packagekit_modify2_built_sources = gs-packagekit-modify2-generated.c gs-packagekit-modify2-generated.h
$(packagekit_modify2_built_sources): Makefile.am org.freedesktop.PackageKit.Modify2.xml
	$(AM_V_GEN) gdbus-codegen					\
		--interface-prefix org.freedesktop.			\
		--c-namespace Gs					\
		--generate-c-code gs-packagekit-modify2-generated	\
		$(srcdir)/org.freedesktop.PackageKit.Modify2.xml

gs-shell-search-provider-generated.h gs-shell-search-provider-generated.c: Makefile.am $(srcdir)/shell-search-provider-dbus-interfaces.xml
	$(AM_V_GEN) gdbus-codegen \
	 	--interface-prefix org.gnome. \
		--c-namespace Gs \
		--generate-c-code gs-shell-search-provider-generated \
		$(srcdir)/shell-search-provider-dbus-interfaces.xml

gs-resources.c: gnome-software.gresource.xml $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/gnome-software.gresource.xml)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --target=$@ --generate-source --c-name gs $(srcdir)/gnome-software.gresource.xml

gs-resources.h: gnome-software.gresource.xml $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/gnome-software.gresource.xml)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --target=$@ --generate-header --c-name gs $(srcdir)/gnome-software.gresource.xml

BUILT_SOURCES =						\
	$(packagekit_built_sources)			\
	$(packagekit_modify2_built_sources)		\
	gs-shell-search-provider-generated.c		\
	gs-shell-search-provider-generated.h		\
	gs-resources.c					\
	gs-resources.h

TESTS_ENVIRONMENT =						\
	libtool --mode=execute valgrind				\
	--quiet							\
	--leak-check=full					\
	--show-possibly-lost=no

check_PROGRAMS =						\
	gs-self-test

gs_self_test_SOURCES =						\
	gs-app.c						\
	gs-category.c						\
	gs-markdown.c						\
	gs-plugin-loader-sync.c					\
	gs-plugin-loader.c					\
	gs-plugin.c						\
	gs-utils.c						\
	gs-self-test.c

gs_self_test_LDADD =						\
	$(APPSTREAM_LIBS)					\
	$(GLIB_LIBS)						\
	$(GTK_LIBS)

gs_self_test_CFLAGS = $(WARN_CFLAGS)

TESTS = gs-self-test

EXTRA_DIST =						\
	shell-search-provider-dbus-interfaces.xml	\
	org.freedesktop.PackageKit.xml			\
	org.freedesktop.PackageKit.Modify2.xml		\
	gnome-software.gresource.xml			\
	gnome-software.xml				\
	gtk-style.css					\
	gtk-style-hc.css				\
	$(desktop_in_files)				\
	org.freedesktop.PackageKit.service.in		\
	org.gnome.Software.service.in			\
	gnome-software-service.desktop.in		\
	$(searchprovider_DATA)				\
	$(UI_FILES)

man_MANS =
if ENABLE_MAN
man_MANS += gnome-software.1
endif

XSLTPROC_FLAGS = \
	--nonet \
	--stringparam man.output.quietly 1 \
	--stringparam funcsynopsis.style ansi \
	--stringparam man.th.extra1.suppress 1 \
	--stringparam man.authors.section.enabled 0 \
	--stringparam man.copyright.section.enabled 0

gnome-software.1: gnome-software.xml
	$(AM_V_GEN) $(XSLTPROC) $(XSLTPROC_FLAGS) http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

clean-local:
	rm -f *~
	rm -f *.1
	rm -f manpage.*

CLEANFILES =						\
	$(BUILT_SOURCES)				\
	gnome-software.1				\
	manpage.*

MAINTAINERCLEANFILES =					\
	*~						\
	Makefile.in

DISTCLEANFILES =					\
	gnome-software-local-file.desktop		\
	gnome-software-service.desktop			\
	org.gnome.Software.desktop			\
	org.freedesktop.PackageKit.service		\
	org.gnome.Software.service

-include $(top_srcdir)/git.mk
