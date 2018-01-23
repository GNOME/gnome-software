plugin_dir = join_paths(get_option('libdir'), 'gs-plugins-11')
plugin_libs = [
  appstream_glib,
  gtk,
  json_glib,
  libsoup
]

subdir('core')
subdir('dpkg')
subdir('dummy')
subdir('epiphany')
subdir('fedora-pkgdb-collections')
subdir('fedora-tagger-usage')

if get_option('enable-flatpak')
  subdir('flatpak')
endif
if get_option('enable-fwupd')
  subdir('fwupd')
endif
if get_option('enable-limba')
   subdir('limba')
endif
if get_option('enable-gudev')
  subdir('modalias')
endif
if get_option('enable-odrs')
  subdir('odrs')
endif
if get_option('enable-packagekit')
  subdir('packagekit')
endif
subdir('repos')
if get_option('enable-rpm-ostree')
  subdir('rpm-ostree')
endif
if get_option('enable-shell-extensions')
  subdir('shell-extensions')
endif
if get_option('enable-snap')
  subdir('snap')
endif
if get_option('enable-steam')
  subdir('steam')
endif
if get_option('enable-ubuntuone')
  subdir('ubuntuone')
endif
if get_option('enable-ubuntu-reviews')
  subdir('ubuntu-reviews')
endif
if get_option('enable-external-appstream')
  subdir('external-appstream')
endif
