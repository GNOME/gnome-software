SUBDIRS =						\
	appdata						\
	tests						\
	icons

appstreamdir = $(datadir)/app-info/xmls
appstream_DATA = org.gnome.Software.xml.gz
appstream_files =					\
	org.gnome.Software-categories.xml		\
	org.gnome.Software-featured.xml			\
	org.gnome.Software-popular.xml
org.gnome.Software.xml.gz: $(appstream_files)
	$(AM_V_GEN) appstream-util merge-appstream $@	\
	$(srcdir)/org.gnome.Software-*.xml

resourcesdir = $(datadir)/gnome-software
dist_resources_DATA =					\
	upgrade-bg.png					\
	featured-ardour.png				\
	featured-ardour-bg.png				\
	featured-blender.png				\
	featured-builder.png				\
	featured-builder-bg.jpg				\
	featured-chess.png				\
	featured-firefox.png				\
	featured-gimp.png				\
	featured-gnome-sudoku.png			\
	featured-inkscape.png				\
	featured-maps.png           \
	featured-maps-bg.png        \
	featured-mypaint.png				\
	featured-polari.svg				\
	featured-transmission.png			\
	featured-weather.png				\
	featured-weather-bg.png

@GSETTINGS_RULES@
gsettings_SCHEMAS = org.gnome.software.gschema.xml

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

BUILT_SOURCES =						\
	org.gnome.Software.xml.gz

CLEANFILES =						\
	$(BUILT_SOURCES)

EXTRA_DIST =						\
	$(appstream_files)				\
	$(gsettings_SCHEMAS)

MAINTAINERCLEANFILES =					\
	*~						\
	Makefile.in

-include $(top_srcdir)/git.mk
