SUBDIRS =						\
	plugins

AM_CPPFLAGS =						\
	$(GLIB_CFLAGS)					\
	$(GTK_CFLAGS)					\
	$(PACKAGEKIT_CFLAGS)				\
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
	gnome-software.ui

bin_PROGRAMS =						\
	gnome-software

gnome_software_SOURCES =				\
	gs-resources.c					\
	gs-resources.h					\
	gs-app.c					\
	gs-app.h					\
	gs-app-widget.c					\
	gs-app-widget.h					\
	gs-plugin.c					\
	gs-plugin.h					\
	gs-plugin-loader.c				\
	gs-plugin-loader.h				\
	gs-plugin-loader-sync.c				\
	gs-plugin-loader-sync.h				\
	ch-markdown.c					\
	ch-markdown.h					\
	egg-list-box.c					\
	egg-list-box.h					\
	gs-main.c

gnome_software_LDADD =					\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	-lm

gnome_software_CFLAGS =					\
	$(WARNINGFLAGS_C)

gs-resources.c: gnome-software.gresource.xml $(srcdir)/gnome-software.ui
	glib-compile-resources --sourcedir=$(srcdir) --target=$@ --generate-source --c-name gs $(srcdir)/gnome-software.gresource.xml
gs-resources.h: gnome-software.gresource.xml
	glib-compile-resources --sourcedir=$(srcdir) --target=$@ --generate-header --c-name gs $(srcdir)/gnome-software.gresource.xml

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
	gs-plugin.c						\
	gs-plugin-loader.c					\
	gs-plugin-loader-sync.c					\
	gs-self-test.c

gs_self_test_LDADD =						\
	$(GLIB_LIBS)						\
	$(GTK_LIBS)

gs_self_test_CFLAGS = $(WARNINGFLAGS_C)

TESTS = gs-self-test

EXTRA_DIST =						\
	gnome-software.gresource.xml			\
	gtk-style.css					\
	$(desktop_in_files)				\
	$(UI_FILES)

clean-local:
	rm -f *~

CLEANFILES = $(BUILT_SOURCES)

MAINTAINERCLEANFILES =					\
	*~						\
	Makefile.in

DISTCLEANFILES =					\
	gnome-software.desktop

-include $(top_srcdir)/git.mk
