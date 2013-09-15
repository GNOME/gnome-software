SUBDIRS =						\
	plugins

AM_CPPFLAGS =						\
	$(GLIB_CFLAGS)					\
	$(GTK_CFLAGS)					\
	$(SOUP_CFLAGS)					\
	$(PACKAGEKIT_CFLAGS)				\
	$(NOTIFY_CFLAGS)				\
	-DG_LOG_DOMAIN=\"Gs\"				\
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
	-DLIBDIR=\"$(libdir)\"				\
	-DBINDIR=\"$(bindir)\"				\
	-DDATADIR=\"$(datadir)\"			\
	-DVERSION="\"$(VERSION)\""			\
	-DLOCALEDIR=\""$(localedir)"\"			\
	-DGS_DATA=\"$(pkgdatadir)\"

@INTLTOOL_DESKTOP_RULE@
desktopdir = $(datadir)/applications
desktop_in_files =					\
	gnome-software.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

UI_FILES =						\
	app-menu.ui					\
	app-tile.ui					\
	app-widget.ui					\
	category-tile.ui				\
	feature-tile.ui					\
	gnome-software.ui				\
	gs-star-widget.ui				\
	screenshot-image.ui				\
	popular-tile.ui

dbusservicedir = $(datadir)/dbus-1/services
dbusservice_DATA = org.gnome.Software.service

org.gnome.Software.service: Makefile
	$(AM_V_GEN) (echo '[D-BUS Service]'; 				\
	 echo 'Name=org.gnome.Software'; 				\
	 echo 'Exec=${libexecdir}/gnome-software-service') > $@.tmp && 	\
	 mv $@.tmp $@

bin_PROGRAMS =						\
	gnome-software

libexec_PROGRAMS =					\
	gnome-software-service

gnome_software_SOURCES =				\
	gs-launcher.c

gnome_software_service_SOURCES =			\
	gs-resources.c					\
	gs-resources.h					\
	gs-utils.c					\
	gs-utils.h					\
	gs-app.c					\
	gs-app.h					\
	gs-category.c					\
	gs-category.h					\
	gs-app-widget.c					\
	gs-app-widget.h					\
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
	gs-box.h					\
	gs-box.c					\
	gs-plugin.c					\
	gs-plugin.h					\
	gs-profile.c					\
	gs-profile.h					\
	gs-screenshot-image.c				\
	gs-screenshot-image.h				\
	gs-screenshot.c					\
	gs-screenshot.h					\
	gs-shell.c					\
	gs-shell.h					\
	gs-shell-details.c				\
	gs-shell-details.h				\
	gs-shell-category.c				\
	gs-shell-category.h				\
	gs-shell-installed.c				\
	gs-shell-installed.h				\
	gs-shell-overview.c				\
	gs-shell-overview.h				\
	gs-shell-updates.c				\
	gs-shell-updates.h				\
	gs-shell-search.c				\
	gs-shell-search.h				\
	gs-plugin-loader.c				\
	gs-plugin-loader.h				\
	gs-plugin-loader-sync.c				\
	gs-plugin-loader-sync.h				\
	gs-application.c				\
	gs-application.h				\
	gs-main.c

gnome_software_LDADD =					\
	$(GLIB_LIBS)

gnome_software_CFLAGS =					\
	$(WARNINGFLAGS_C)

gnome_software_service_LDADD =				\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)					\
	$(SOUP_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	$(NOTIFY_LIBS)					\
	-lm

gnome_software_service_CFLAGS =				\
	$(WARNINGFLAGS_C)

gs-resources.c: gnome-software.gresource.xml $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/gnome-software.gresource.xml)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --target=$@ --generate-source --c-name gs $(srcdir)/gnome-software.gresource.xml

gs-resources.h: gnome-software.gresource.xml $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/gnome-software.gresource.xml)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --target=$@ --generate-header --c-name gs $(srcdir)/gnome-software.gresource.xml

BUILT_SOURCES =						\
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
	gs-plugin-loader-sync.c					\
	gs-plugin-loader.c					\
	gs-plugin.c						\
	gs-profile.c						\
	gs-screenshot.c						\
	gs-utils.c						\
	gs-self-test.c

gs_self_test_LDADD =						\
	$(NOTIFY_LIBS)						\
	$(GLIB_LIBS)						\
	$(GTK_LIBS)

gs_self_test_CFLAGS = $(WARNINGFLAGS_C)

TESTS = gs-self-test

EXTRA_DIST =						\
	gnome-software.gresource.xml			\
	gnome-software.xml				\
	gtk-style.css					\
	gtk-style-hc.css					\
	shadow.png					\
	shadow-active.png				\
	$(desktop_in_files)				\
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
	gnome-software.desktop

-include $(top_srcdir)/git.mk
