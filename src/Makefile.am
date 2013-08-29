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
	gnome-software.ui				\
	app-menu.ui

bin_PROGRAMS =						\
	gnome-software

gnome_software_SOURCES =				\
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
	gs-plugin.c					\
	gs-plugin.h					\
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
	ch-markdown.c					\
	ch-markdown.h					\
	gs-application.c				\
	gs-application.h				\
	gs-main.c

gnome_software_LDADD =					\
	$(GLIB_LIBS)					\
	$(GTK_LIBS)					\
	$(PACKAGEKIT_LIBS)				\
	-lm

gnome_software_CFLAGS =					\
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
	gnome-software.sgml				\
	gtk-style.css					\
	$(desktop_in_files)				\
	$(UI_FILES)

man_MANS =
if ENABLE_MAN
man_MANS += gnome-software.1
endif

gnome-software.1: gnome-software.sgml
	docbook2man $? > /dev/null

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
