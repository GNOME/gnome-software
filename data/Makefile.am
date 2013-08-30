resourcesdir = $(datadir)/gnome-software
dist_resources_DATA =					\
	featured-gimp.png				\
	featured-firefox.png				\
	featured-xchat.png				\
	featured-gnome-weather.png			\
	featured-gnome-sudoku.png			\
	stars0.png					\
	stars1.png					\
	stars2.png					\
	stars3.png					\
	stars4.png

appdatadir = $(datadir)/appdata
appdata_files = gnome-software.appdata.xml

EXTRA_DIST =						\
	$(appdata_files)

MAINTAINERCLEANFILES =					\
	*~						\
	Makefile.in

-include $(top_srcdir)/git.mk
