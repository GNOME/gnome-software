subdir('plugins')

cargs = ['-DG_LOG_DOMAIN="Gpm"']

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
  'gnome-software.h',
  'gs-utils.c',
  'gs-utils.h',
  'gs-app.c',
  'gs-app.h',
  'gs-app-private.h',
  'gs-app-list.c',
  'gs-app-list.h',
  'gs-app-list-private.h',
  'gs-auth.c',
  'gs-auth.h',
  'gs-auth-dialog.c',
  'gs-auth-dialog.h',
  'gs-category.c',
  'gs-category.h',
  'gs-category-private.h',
  'gs-common.c',
  'gs-common.h',
  'gs-content-rating.c',
  'gs-content-rating.h',
  'gs-debug.c',
  'gs-debug.h',
  'gs-app-addon-row.c',
  'gs-app-addon-row.h',
  'gs-app-row.c',
  'gs-app-row.h',
  'gs-star-widget.c',
  'gs-star-widget.h',
  'gs-popular-tile.c',
  'gs-popular-tile.h',
  'gs-feature-tile.c',
  'gs-feature-tile.h',
  'gs-category-tile.c',
  'gs-category-tile.h',
  'gs-app-tile.c',
  'gs-app-tile.h',
  'gs-summary-tile.c',
  'gs-summary-tile.h',
  'gs-app-folder-dialog.c',
  'gs-app-folder-dialog.h',
  'gs-first-run-dialog.c',
  'gs-first-run-dialog.h',
  'gs-history-dialog.c',
  'gs-history-dialog.h',
  'gs-fixed-size-bin.h',
  'gs-fixed-size-bin.c',
  'gs-hiding-box.h',
  'gs-hiding-box.c',
  'gs-info-bar.h',
  'gs-info-bar.c',
  'gs-language.c',
  'gs-language.h',
  'gs-os-release.c',
  'gs-os-release.h',
  'gs-page.c',
  'gs-page.h',
  'gs-plugin.c',
  'gs-plugin.h',
  'gs-plugin-event.c',
  'gs-plugin-event.h',
  'gs-plugin-private.h',
  'gs-plugin-types.h',
  'gs-plugin-vfuncs.h',
  'gs-progress-button.c',
  'gs-progress-button.h',
  'gs-removal-dialog.c',
  'gs-removal-dialog.h',
  'gs-review-bar.c',
  'gs-review-bar.h',
  'gs-review-dialog.c',
  'gs-review-dialog.h',
  'gs-review-histogram.c',
  'gs-review-histogram.h',
  'gs-review-row.c',
  'gs-review-row.h',
  'gs-screenshot-image.c',
  'gs-screenshot-image.h',
  'gs-shell.c',
  'gs-shell.h',
  'gs-shell-details.c',
  'gs-shell-details.h',
  'gs-shell-category.c',
  'gs-shell-category.h',
  'gs-shell-extras.c',
  'gs-shell-extras.h',
  'gs-shell-installed.c',
  'gs-shell-installed.h',
  'gs-shell-moderate.c',
  'gs-shell-moderate.h',
  'gs-shell-loading.c',
  'gs-shell-loading.h',
  'gs-shell-overview.c',
  'gs-shell-overview.h',
  'gs-shell-updates.c',
  'gs-shell-updates.h',
  'gs-shell-search.c',
  'gs-shell-search.h',
  'gs-sources-dialog.c',
  'gs-sources-dialog.h',
  'gs-sources-dialog-row.c',
  'gs-sources-dialog-row.h',
  'gs-update-dialog.c',
  'gs-update-dialog.h',
  'gs-update-list.c',
  'gs-update-list.h',
  'gs-update-monitor.c',
  'gs-update-monitor.h',
  'gs-upgrade-banner.c',
  'gs-upgrade-banner.h',
  'gs-vendor.c',
  'gs-vendor.h',
  'gs-plugin-loader.c',
  'gs-plugin-loader.h',
  'gs-plugin-loader-sync.c',
  'gs-plugin-loader-sync.h',
  'gs-shell-search-provider.c',
  'gs-shell-search-provider.h',
  'gs-application.c',
  'gs-application.h',
  'gs-folders.c',
  'gs-folders.h',
  'gs-main.c'
]

gnome_software_dependencies = [gtk, gio_unix, libm, appstream_glib, json_glib, libsoup, libsecret, gmodule]

if get_option('enable-packagekit')
  gnome_software_sources += [
    'gs-dbus-helper.c',
    'gs-dbus-helper.h',
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
