SUBDIRS =						\
	appdata						\
	tests						\
	icons

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
	featured-inkscape.svg				\
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

polkit_policydir = $(datadir)/polkit-1/actions
polkit_policy_in_files = org.gnome.software.external-appstream.policy.in
polkit_policy_DATA = $(polkit_policy_in_files:.policy.in=.policy)

%.policy: %.policy.in Makefile
	$(AM_V_GEN) sed -e "s|@libexecdir[@]|$(libexecdir)|g" $< > $@

EXTRA_DIST =							\
	org.gnome.software.external-appstream.policy.in		\
	$(gsettings_SCHEMAS)

CLEANFILES =						\
	org.gnome.software.external-appstream.policy

MAINTAINERCLEANFILES =					\
	*~						\
	Makefile.in

-include $(top_srcdir)/git.mk
