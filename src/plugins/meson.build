plugin_dir = join_paths(get_option('libdir'), 'gs-plugins-11')

plugin_libs = [
  appstream_glib,
  gtk,
  json_glib,
  libsoup
]
metainfos = []

shared_module('gs_plugin_dummy',
  sources : 'gs-plugin-dummy.c',
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  dependencies : [appstream_glib, gtk, libsoup]
)

shared_module('gs_plugin_dpkg',
  sources : 'gs-plugin-dpkg.c',
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  dependencies : plugin_libs
)

if get_option('enable-external-appstream')
  executable(
    'gnome-software-install-appstream',
    sources : 'gs-install-appstream.c',
    include_directories : [
      include_directories('@0@/..'.format(meson.current_build_dir())),
    ],
    dependencies : [appstream_glib],
    c_args : cargs,
    install : true,
    install_dir : 'libexec'
  )
  shared_module('gs_plugin_external-appstream',
  sources : 'gs-plugin-external-appstream.c',
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : [gio_unix, appstream_glib, libsoup]
  )
endif

shared_module('gs_plugin_key-colors',
  sources : 'gs-plugin-key-colors.c',
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  dependencies : plugin_libs
)

if get_option('enable-shell-extensions')
  shared_module('gs_plugin_shell-extensions',
  sources : 'gs-plugin-shell-extensions.c',
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : plugin_libs
  )
endif

shared_module('gs_plugin_fedora-distro-upgrades',
  sources : 'gs-plugin-fedora-distro-upgrades.c',
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  dependencies : plugin_libs
)

if get_option('enable-gudev')
  shared_module('gs_plugin_modalias',
  sources : 'gs-plugin-modalias.c',
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : [ plugin_libs, gudev ]
  )
endif

shared_module('gs_plugin_provenance',
  sources : 'gs-plugin-provenance.c',
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  dependencies : plugin_libs
)

shared_module('gs_plugin_provenance-license',
  sources : 'gs-plugin-provenance-license.c',
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  dependencies : plugin_libs
)

shared_module('gs_plugin_fedora-tagger-usage',
  sources : 'gs-plugin-fedora-tagger-usage.c',
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  dependencies : plugin_libs
)

shared_module('gs_plugin_epiphany',
  sources : 'gs-plugin-epiphany.c',
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  dependencies : plugin_libs
)
metainfos += 'org.gnome.Software.Plugin.Epiphany.metainfo.xml'

shared_module('gs_plugin_icons',
  sources : 'gs-plugin-icons.c',
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  dependencies : plugin_libs
)

shared_module('gs_plugin_appstream',
  sources : [
    'gs-appstream.c',
    'gs-plugin-appstream.c'
  ],
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  dependencies : plugin_libs
)

if get_option('enable-limba')
shared_module('gs_plugin_limba',
  sources : 'gs-plugin-limba.c',
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  dependencies : plugin_libs
  )
endif

if get_option('enable-flatpak')
  shared_module('gs_plugin_flatpak',
    sources : [
      'gs-appstream.c',
      'gs-flatpak.c',
      'gs-flatpak-symlinks.c',
      'gs-plugin-flatpak.c'
    ],
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : [ plugin_libs, flatpak ]
  )
  metainfos += 'org.gnome.Software.Plugin.Flatpak.metainfo.xml'
endif

if get_option('enable-ostree')
  shared_module('gs_plugin_ostree',
    sources : 'gs-plugin-ostree.c',
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : [ plugin_libs, ostree ]
  )
endif

if get_option('enable-odrs')
  shared_module('gs_plugin_odrs',
    sources : 'gs-plugin-odrs.c',
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : plugin_libs
  )
  metainfos += 'org.gnome.Software.Plugin.Odrs.metainfo.xml'
endif

if get_option('enable-rpm')
  shared_module('gs_plugin_rpm',
    sources : 'gs-plugin-rpm.c',
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : [ plugin_libs, rpm ]
  )
endif

if get_option('enable-steam')
  shared_module('gs_plugin_steam',
    sources : 'gs-plugin-steam.c',
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : plugin_libs
  )
  metainfos += 'org.gnome.Software.Plugin.Steam.metainfo.xml'
endif

shared_module('gs_plugin_repos',
  sources : 'gs-plugin-repos.c',
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  dependencies : plugin_libs
)

shared_module('gs_plugin_desktop-categories',
  sources : [
    'gs-plugin-desktop-categories.c',
    'gs-desktop-common.c',
  ],
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  dependencies : plugin_libs
)

shared_module('gs_plugin_desktop-menu-path',
  sources : [
    'gs-plugin-desktop-menu-path.c',
    'gs-desktop-common.c',
  ],
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  dependencies : plugin_libs
)

shared_module('gs_plugin_hardcoded-blacklist',
  sources : 'gs-plugin-hardcoded-blacklist.c',
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  dependencies : plugin_libs
)

shared_module('gs_plugin_hardcoded-popular',
  sources : 'gs-plugin-hardcoded-popular.c',
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  dependencies : plugin_libs
)

shared_module('gs_plugin_hardcoded-featured',
  sources : 'gs-plugin-hardcoded-featured.c',
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  dependencies : plugin_libs
)

if get_option('enable-ubuntu-reviews')
  shared_module('gs_plugin_ubuntu-reviews',
    sources : 'gs-plugin-ubuntu-reviews.c',
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : [ plugin_libs, oauth ]
  )
endif

if get_option('enable-firmware')
  shared_module('gs_plugin_fwupd',
    sources : 'gs-plugin-fwupd.c',
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : [ plugin_libs, fwupd ]
  )
  metainfos += 'org.gnome.Software.Plugin.Fwupd.metainfo.xml'
endif

if get_option('enable-packagekit')
  shared_module('gs_plugin_systemd-updates',
    sources : [
      'gs-plugin-systemd-updates.c',
      'packagekit-common.c',
    ],
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : [ plugin_libs, packagekit ]
  )

  shared_module('gs_plugin_packagekit',
    sources : [
      'gs-plugin-packagekit.c',
      'packagekit-common.c',
    ],
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : [ plugin_libs, packagekit ]
  )

  shared_module('gs_plugin_packagekit-refine',
    sources : [
      'gs-plugin-packagekit-refine.c',
      'gs-markdown.c',
      'packagekit-common.c',
    ],
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : [ plugin_libs, packagekit ]
  )

  shared_module('gs_plugin_packagekit-refresh',
    sources : [
      'gs-plugin-packagekit-refresh.c',
      'packagekit-common.c',
    ],
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : [ plugin_libs, packagekit ]
  )

  shared_module('gs_plugin_packagekit-local',
    sources : [
      'gs-plugin-packagekit-local.c',
      'packagekit-common.c',
    ],
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : [ plugin_libs, packagekit ]
  )

  shared_module('gs_plugin_packagekit-history',
  sources : 'gs-plugin-packagekit-history.c',
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : [ plugin_libs, packagekit ]
  )

  shared_module('gs_plugin_packagekit-upgrade',
    sources : [
      'gs-plugin-packagekit-upgrade.c',
      'packagekit-common.c',
    ],
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : [ plugin_libs, packagekit ]
  )

  shared_module('gs_plugin_packagekit-offline',
  sources : 'gs-plugin-packagekit-offline.c',
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : [ plugin_libs, packagekit ]
  )

  shared_module('gs_plugin_packagekit-origin',
    sources : [
      'gs-plugin-packagekit-origin.c',
      'packagekit-common.c',
    ],
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : [ plugin_libs, packagekit ]
  )

  shared_module('gs_plugin_packagekit-proxy',
  sources : 'gs-plugin-packagekit-proxy.c',
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : [ plugin_libs, packagekit ])
endif

if get_option('enable-snap')
  shared_module('gs_plugin_snap',
    sources : [
      'gs-plugin-snap.c',
      'gs-snapd.c'
    ],
    include_directories : [
      include_directories('..'),
      include_directories('../..'),
    ],
    install : true,
    install_dir: plugin_dir,
    dependencies : [ plugin_libs, snap ]
  )
  metainfos += 'org.gnome.Software.Plugin.Snap.metainfo.xml'
endif

shared_module('gs_plugin_ubuntuone',
  sources : 'gs-plugin-ubuntuone.c',
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  dependencies : plugin_libs
)

foreach metainfo: metainfos
  i18n.merge_file(
    input: metainfo + '.in',
    output: metainfo,
    type: 'xml',
    po_dir: join_paths(meson.source_root(), 'po'),
    install: true,
    install_dir: join_paths(get_option('datadir'), 'appdata')
  )
endforeach

if get_option('enable-tests')
  e = executable('gs-self-test2',
    sources : [
      'gs-markdown.c',
      'gs-self-test.c'
    ],
    include_directories : [
      include_directories('../..'),
    ],
    dependencies : [gtk, gio_unix, libm, appstream_glib ]
  )
  test('gs-self-test', e)
endif
