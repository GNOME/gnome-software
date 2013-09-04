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

if ENABLE_OFFLINE_APPSTREAM_DATA
fedora-20.xml.gz:
	wget -O fedora-20.xml.gz http://people.freedesktop.org/~hughsient/temp/appstream.xml.gz
fedora-20.tar.gz:
	wget -O fedora-20.tar.gz http://people.freedesktop.org/~hughsient/temp/appstream-icons.tar.gz

appstreamxmldir = $(datadir)/app-info/xmls
dist_appstreamxml_DATA = fedora-20.xml.gz
appstreamiconsdir = $(datadir)/app-info/icons
dist_appstreamicons_DATA = fedora-20.tar.gz
endif

EXTRA_DIST =						\
	$(appdata_files)

CLEANFILES =						\
	$(dist_appstreamxml_DATA)			\
	$(dist_appstreamicons_DATA)

MAINTAINERCLEANFILES =					\
	*~						\
	Makefile.in

-include $(top_srcdir)/git.mk
