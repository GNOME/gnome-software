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

appstream.xml.gz:
	wget -N http://people.freedesktop.org/~hughsient/temp/appstream.xml.gz
appstream-icons.tar.gz:
	wget -N http://people.freedesktop.org/~hughsient/temp/appstream-icons.tar.gz

appstreamdir = $(datadir)/gnome-software
dist_appstream_DATA = appstream.xml.gz appstream-icons.tar.gz

EXTRA_DIST =						\
	$(appdata_files)

CLEANFILES =						\
	$(dist_appstream_DATA)

MAINTAINERCLEANFILES =					\
	*~						\
	Makefile.in

-include $(top_srcdir)/git.mk
