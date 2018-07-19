plugin_dir = join_paths(get_option('libdir'), 'gs-plugins-' + gs_plugin_api_version)
plugin_libs = [
  appstream_glib,
  gio_unix,
  gtk,
  json_glib,
  libsoup
]

subdir('core')
subdir('dpkg')
subdir('dummy')
subdir('epiphany')
subdir('fedora-pkgdb-collections')

if get_option('flatpak')
  subdir('flatpak')
endif
if get_option('fwupd')
  subdir('fwupd')
endif
if get_option('gudev')
  subdir('modalias')
endif
if get_option('odrs')
  subdir('odrs')
endif
if get_option('packagekit')
  subdir('packagekit')
endif
subdir('repos')
if get_option('rpm_ostree')
  subdir('rpm-ostree')
endif
if get_option('shell_extensions')
  subdir('shell-extensions')
endif
if get_option('snap')
  subdir('snap')
endif
if get_option('steam')
  subdir('steam')
endif
if get_option('ubuntuone')
  subdir('ubuntuone')
endif
if get_option('ubuntu_reviews')
  subdir('ubuntu-reviews')
endif
if get_option('external_appstream')
  subdir('external-appstream')
endif
