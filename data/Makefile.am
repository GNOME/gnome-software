resourcesdir = $(datadir)/gnome-software
dist_resources_DATA =					\
	featured-gimp.png				\
	featured-firefox.png				\
	featured-xchat.png				\
	featured-org.gnome.Weather.Application.png	\
	featured-gnome-sudoku.png			\
	stars0.png					\
	stars1.png					\
	stars2.png					\
	stars3.png					\
	stars4.png

appdatadir = $(datadir)/appdata
appdata_files = gnome-software.appdata.xml

install-sample-data:
	wget -O fedora-20.xml.gz http://people.freedesktop.org/~hughsient/temp/fedora-20.xml.gz; \
	mkdir -p $(DESTDIR)$(datadir)/app-info/xmls; \
	cp fedora-20.xml.gz $(DESTDIR)$(datadir)/app-info/xmls/; \
	wget -O fedora-20-icons.tar.gz http://people.freedesktop.org/~hughsient/temp/fedora-20-icons.tar.gz; \
	cp fedora-20-icons.tar.gz $(DESTDIR)$(datadir)/app-info/icons; \
	cd $(DESTDIR)$(datadir)/app-info/icons; \
	tar xvzf fedora-20-icons.tar.gz &> /dev/null; \
	rm fedora-20-icons.tar.gz; \
	cd -

EXTRA_DIST =						\
	$(appdata_files)

MAINTAINERCLEANFILES =					\
	*~						\
	Makefile.in

-include $(top_srcdir)/git.mk
