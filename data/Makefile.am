SUBDIRS =						\
	icons

resourcesdir = $(datadir)/gnome-software
dist_resources_DATA =					\
	featured-darktable.png				\
	featured-firefox.png				\
	featured-gimp.png				\
	featured-gnome-sudoku.png			\
	featured-mypaint.png				\
	featured-weather.png         \
	featured-weather-bg.png   \
	featured-xchat.png				\
	featured.ini					\
	stars0.png					\
	stars1.png					\
	stars2.png					\
	stars3.png					\
	stars4.png          

@INTLTOOL_XML_RULE@
appdatadir = $(datadir)/appdata
dist_appdata_DATA = $(appdata_in_files:.xml.in=.xml)
appdata_in_files = gnome-software.appdata.xml.in

install-sample-data:
	wget -O fedora-20.xml.gz http://people.freedesktop.org/~hughsient/temp/fedora-20.xml.gz; \
	mkdir -p $(DESTDIR)$(datadir)/app-info/xmls; \
	cp fedora-20.xml.gz $(DESTDIR)$(datadir)/app-info/xmls/; \
	wget -O fedora-20-icons.tar.gz http://people.freedesktop.org/~hughsient/temp/fedora-20-icons.tar.gz; \
	mkdir -p $(DESTDIR)$(datadir)/app-info/icons/fedora-20; \
	cp fedora-20-icons.tar.gz $(DESTDIR)$(datadir)/app-info/icons/fedora-20; \
	cd $(DESTDIR)$(datadir)/app-info/icons/fedora-20; \
	tar xvzf fedora-20-icons.tar.gz &> /dev/null; \
	rm fedora-20-icons.tar.gz; \
	cd -

EXTRA_DIST =						\
	$(appdata_in_files)				\
	featured.ini.in

CLEANFILES =						\
	gnome-software.appdata.xml

MAINTAINERCLEANFILES =					\
	*~						\
	Makefile.in

-include $(top_srcdir)/git.mk
