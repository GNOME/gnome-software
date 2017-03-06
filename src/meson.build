subdir('plugins')

cargs = ['-DG_LOG_DOMAIN="Gs"']

resources_src = gnome.compile_resources(
  'gs-resources',
  'gnome-software.gresource.xml',
  source_dir : '.',
  c_name : 'gs'
)

gdbus_src = gnome.gdbus_codegen(
  'gs-shell-search-provider-generated',
  'shell-search-provider-dbus-interfaces.xml',
  interface_prefix : 'org.gnome.',
  namespace : 'Gs'
)

gnome_software_sources = [
  'gs-utils.c',
  'gs-app.c',
  'gs-app-list.c',
  'gs-auth.c',
  'gs-auth-dialog.c',
  'gs-category.c',
  'gs-common.c',
  'gs-content-rating.c',
  'gs-debug.c',
  'gs-app-addon-row.c',
  'gs-app-row.c',
  'gs-star-widget.c',
  'gs-popular-tile.c',
  'gs-feature-tile.c',
  'gs-category-tile.c',
  'gs-app-tile.c',
  'gs-summary-tile.c',
  'gs-app-folder-dialog.c',
  'gs-first-run-dialog.c',
  'gs-history-dialog.c',
  'gs-fixed-size-bin.c',
  'gs-hiding-box.c',
  'gs-info-bar.c',
  'gs-language.c',
  'gs-os-release.c',
  'gs-page.c',
  'gs-plugin.c',
  'gs-plugin-event.c',
  'gs-progress-button.c',
  'gs-removal-dialog.c',
  'gs-review-bar.c',
  'gs-review-dialog.c',
  'gs-review-histogram.c',
  'gs-review-row.c',
  'gs-screenshot-image.c',
  'gs-shell.c',
  'gs-details-page.c',
  'gs-category-page.c',
  'gs-extras-page.c',
  'gs-installed-page.c',
  'gs-moderate-page.c',
  'gs-loading-page.c',
  'gs-overview-page.c',
  'gs-updates-page.c',
  'gs-search-page.c',
  'gs-sources-dialog.c',
  'gs-sources-dialog-row.c',
  'gs-update-dialog.c',
  'gs-update-list.c',
  'gs-update-monitor.c',
  'gs-upgrade-banner.c',
  'gs-vendor.c',
  'gs-plugin-loader.c',
  'gs-plugin-loader-sync.c',
  'gs-shell-search-provider.c',
  'gs-application.c',
  'gs-folders.c',
  'gs-main.c'
]

gnome_software_dependencies = [
  appstream_glib,
  gio_unix,
  gmodule,
  gtk,
  json_glib,
  libm,
  libsecret,
  libsoup
]

if get_option('enable-packagekit')
  gnome_software_sources += [
    'gs-dbus-helper.c',
  ]
  gnome_software_sources += gnome.gdbus_codegen(
    'gs-packagekit-generated',
    'org.freedesktop.PackageKit.xml',
    interface_prefix : 'org.freedesktop.',
    namespace : 'Gs'
  )
  gnome_software_sources += gnome.gdbus_codegen(
    'gs-packagekit-modify2-generated',
    'org.freedesktop.PackageKit.Modify2.xml',
    interface_prefix : 'org.freedesktop.',
    namespace : 'Gs'
  )
  gnome_software_dependencies += [packagekit]
endif

executable(
  'gnome-software',
  resources_src,
  gdbus_src,
  sources : gnome_software_sources,
  include_directories : [
    include_directories('..'),
  ],
  dependencies : gnome_software_dependencies,
  c_args : cargs,
  install : true,
  install_dir : 'bin'
)

executable(
  'gnome-software-cmd',
  sources : [
    'gs-app.c',
    'gs-app-list.c',
    'gs-auth.c',
    'gs-cmd.c',
    'gs-common.c',
    'gs-debug.c',
    'gs-utils.c',
    'gs-os-release.c',
    'gs-plugin-event.c',
    'gs-plugin-loader.c',
    'gs-plugin-loader-sync.c',
    'gs-category.c',
    'gs-plugin.c'
  ],
  include_directories : [
    include_directories('..'),
  ],
  dependencies : [
    appstream_glib,
    gio_unix,
    gmodule,
    gtk,
    json_glib,
    libm,
    libsecret,
    libsoup
  ],
  c_args : cargs,
  install : true,
  install_dir : 'libexec'
)

if get_option('enable-tests')
  e = executable(
    'gs-self-test',
    sources : [
      'gs-app.c',
      'gs-app-list.c',
      'gs-auth.c',
      'gs-category.c',
      'gs-common.c',
      'gs-os-release.c',
      'gs-plugin-event.c',
      'gs-plugin-loader-sync.c',
      'gs-plugin-loader.c',
      'gs-plugin.c',
      'gs-utils.c',
      'gs-self-test.c'
    ],
    include_directories : [
      include_directories('..'),
    ],
    dependencies : [
      appstream_glib,
      gio_unix,
      gmodule,
      gtk,
      json_glib,
      libm,
      libsecret,
      libsoup
    ],
    c_args : cargs
  )
  test('gs-self-test', e)
endif

install_headers([
    'gnome-software.h',
    'gs-app.h',
    'gs-app-list.h',
    'gs-auth.h',
    'gs-category.h',
    'gs-os-release.h',
    'gs-plugin.h',
    'gs-plugin-event.h',
    'gs-plugin-types.h',
    'gs-plugin-vfuncs.h',
    'gs-utils.h'
  ],
  subdir : 'gnome-software'
)

# replace @VERSION@
configure_file(
  input : 'org.gnome.Software.service.in',
  output : 'org.gnome.Software.service',
  install: true,
  install_dir: join_paths(get_option('datadir'), 'dbus-1/services'),
  configuration : conf
)

# replace @bindir@
configure_file(
  input : 'gnome-software-service.desktop.in',
  output : 'gnome-software-service.desktop',
  install: true,
  install_dir: join_paths(get_option('sysconfdir'), 'xdg/autostart'),
  configuration : conf
)

i18n.merge_file(
  input: 'org.gnome.Software.desktop.in',
  output: 'org.gnome.Software.desktop',
  type: 'desktop',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

i18n.merge_file(
  input: 'gnome-software-local-file.desktop.in',
  output: 'gnome-software-local-file.desktop',
  type: 'desktop',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

install_data('org.gnome.Software-search-provider.ini',
             install_dir : 'share/gnome-shell/search-providers')

if get_option('enable-man')
  xsltproc = find_program('xsltproc')
  custom_target('manfile',
    input: 'gnome-software.xml',
    output: 'gnome-software.1',
    install: true,
    install_dir: join_paths(get_option('mandir'), 'man1'),
    command: [
      xsltproc,
      '--nonet',
      '--stringparam', 'man.output.quietly', '1',
      '--stringparam', 'funcsynopsis.style', 'ansi',
      '--stringparam', 'man.th.extra1.suppress', '1',
      '--stringparam', 'man.authors.section.enabled', '0',
      '--stringparam', 'man.copyright.section.enabled', '0',
      '-o', '@OUTPUT@',
      'http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl',
      '@INPUT@'
    ]
  )
endif

if get_option('enable-packagekit')
  # replace @bindir@
  configure_file(
    input : 'org.freedesktop.PackageKit.service.in',
    output : 'org.freedesktop.PackageKit.service',
    install: true,
    install_dir: join_paths(get_option('datadir'), 'dbus-1', 'services'),
    configuration : conf
  )
endif

configure_file(
  input : 'gnome-software.pc.in',
  output : 'gnome-software.pc',
  install: true,
  install_dir: join_paths(get_option('libdir'), 'pkgconfig'),
  configuration : conf
)
