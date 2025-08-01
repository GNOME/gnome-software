cargs = ['-DG_LOG_DOMAIN="Gs"']
cargs += ['-DLOCALPLUGINDIR=""']

resources_src = gnome.compile_resources(
  'gs-resources',
  'gnome-software.gresource.xml',
  source_dir: '.',
  c_name: 'gs'
)

gdbus_src = gnome.gdbus_codegen(
  'gs-shell-search-provider-generated',
  'shell-search-provider-dbus-interfaces.xml',
  interface_prefix: 'org.gnome.',
  namespace: 'Gs',
  extra_args: [ '--glib-min-required=' + glib.version() ],
)

enums = gnome.mkenums_simple('gs-enums',
  sources: [
    'gs-context-dialog-row.h',
    'gs-review-row.h',
    'gs-updates-paused-banner.h',
  ],
  install_header: false,
)

gnome_software_sources = [
  'gs-age-rating-context-dialog.c',
  'gs-app-addon-row.c',
  'gs-app-reviews-dialog.c',
  'gs-app-version-history-dialog.c',
  'gs-app-version-history-row.c',
  'gs-application.c',
  'gs-app-context-bar.c',
  'gs-app-details-page.c',
  'gs-app-row.c',
  'gs-app-tile.c',
  'gs-app-translation-dialog.c',
  'gs-basic-auth-dialog.c',
  'gs-category-page.c',
  'gs-category-tile.c',
  'gs-common.c',
  'gs-context-dialog-row.c',
  'gs-css.c',
  'gs-description-box.c',
  'gs-details-page.c',
  'gs-extras-page.c',
  'gs-feature-tile.c',
  'gs-featured-carousel.c',
  'gs-hardware-support-context-dialog.c',
  'gs-info-window.c',
  'gs-installed-page.c',
  'gs-language.c',
  'gs-layout-manager.c',
  'gs-license-tile.c',
  'gs-loading-page.c',
  'gs-lozenge.c',
  'gs-main.c',
  'gs-overview-page.c',
  'gs-origin-popover-row.c',
  'gs-os-update-page.c',
  'gs-page.c',
  'gs-prefs-dialog.c',
  'gs-progress-button.c',
  'gs-removal-dialog.c',
  'gs-repos-dialog.c',
  'gs-repos-section.c',
  'gs-repo-row.c',
  'gs-review-bar.c',
  'gs-review-dialog.c',
  'gs-review-histogram.c',
  'gs-review-row.c',
  'gs-safety-context-dialog.c',
  'gs-screenshot-carousel.c',
  'gs-screenshot-image.c',
  'gs-search-page.c',
  'gs-shell.c',
  'gs-shell-search-provider.c',
  'gs-star-image.c',
  'gs-star-widget.c',
  'gs-storage-context-dialog.c',
  'gs-summary-tile.c',
  'gs-toast.c',
  'gs-update-dialog.c',
  'gs-update-list.c',
  'gs-update-monitor.c',
  'gs-updates-page.c',
  'gs-updates-paused-banner.c',
  'gs-updates-section.c',
  'gs-upgrade-banner.c',
  'gs-vendor.c'
]

gnome_software_dependencies = [
  appstream,
  gio_unix,
  glib,
  gmodule,
  gtk,
  json_glib,
  libgnomesoftware_dep,
  libadwaita,
  libm,
  libsoup,
  libxmlb,
]

if get_option('dkms')
  gnome_software_sources += [
    'gs-dkms-dialog.h',
    'gs-dkms-dialog.c',
  ]
endif

if get_option('packagekit')
  gnome_software_sources += [
    'gs-dbus-helper.c',
  ]
  gnome_software_sources += gnome.gdbus_codegen(
    'gs-packagekit-generated',
    'org.freedesktop.PackageKit.xml',
    interface_prefix: 'org.freedesktop.',
    namespace: 'Gs',
    extra_args: [ '--glib-min-required=' + glib.version() ],
  )
  gnome_software_sources += gnome.gdbus_codegen(
    'gs-packagekit-modify2-generated',
    'org.freedesktop.PackageKit.Modify2.xml',
    interface_prefix: 'org.freedesktop.',
    namespace: 'Gs',
    extra_args: [ '--glib-min-required=' + glib.version() ],
  )
  gnome_software_dependencies += [packagekit]
endif

if get_option('mogwai')
  gnome_software_dependencies += [mogwai_schedule_client]
endif

executable(
  'gnome-software',
  resources_src,
  gdbus_src,
  sources: gnome_software_sources + enums,
  include_directories: [
    include_directories('..'),
    include_directories('../lib'),
  ],
  dependencies: gnome_software_dependencies,
  c_args: cargs,
  install: true,
  install_dir: get_option('bindir'),
  install_rpath: gs_private_libdir,
)

executable(
  'gnome-software-restarter',
  sources: 'gs-restarter.c',
  include_directories: [
    include_directories('..'),
  ],
  dependencies: [
    gio_unix,
    glib,
  ],
  c_args: cargs,
  install: true,
  install_dir: get_option('libexecdir')
)

# no quoting
cdata = configuration_data()
cdata.set('bindir', join_paths(get_option('prefix'),
                               get_option('bindir')))
if (get_option('apt'))
  cdata.set('apthandler', 'x-scheme-handler/apt;')
else
  cdata.set('apthandler', '')
endif
if (get_option('snap'))
  cdata.set('snaphandler', 'x-scheme-handler/snap;')
else
  cdata.set('snaphandler', '')
endif
if (get_option('flatpak'))
  cdata.set('flatpakhandler', 'x-scheme-handler/flatpak+https;')
else
  cdata.set('flatpakhandler', '')
endif
cdata.set('application_id', application_id)

# replace @bindir@
configure_file(
  input: 'org.gnome.Software.service.in',
  output: application_id + '.service',
  install_dir: join_paths(get_option('datadir'), 'dbus-1/services'),
  configuration: cdata
)

i18n.merge_file(
  input:
    # replace mime-type handlers
    configure_file(
      input: 'org.gnome.Software.desktop.in',
      output: 'org.gnome.Software.desktop.tmp',
      configuration: cdata
    ),
  output: application_id + '.desktop',
  type: 'desktop',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

if get_option('flatpak')
  i18n.merge_file(
    input:
     configure_file(
       input: 'gnome-software-local-file-flatpak.desktop.in',
       output: 'gnome-software-local-file-flatpak.desktop.tmp',
       configuration: cdata
     ),
    output: 'gnome-software-local-file-flatpak.desktop',
    type: 'desktop',
    po_dir: join_paths(meson.project_source_root(), 'po'),
    install: true,
    install_dir: join_paths(get_option('datadir'), 'applications')
  )
endif

if get_option('fwupd')
  i18n.merge_file(
    input:
     configure_file(
       input: 'gnome-software-local-file-fwupd.desktop.in',
       output: 'gnome-software-local-file-fwupd.desktop.tmp',
       configuration: cdata
     ),
    output: 'gnome-software-local-file-fwupd.desktop',
    type: 'desktop',
    po_dir: join_paths(meson.project_source_root(), 'po'),
    install: true,
    install_dir: join_paths(get_option('datadir'), 'applications')
  )
endif

if get_option('packagekit') or get_option('rpm_ostree')
  i18n.merge_file(
    input:
     configure_file(
       input: 'gnome-software-local-file-packagekit.desktop.in',
       output: 'gnome-software-local-file-packagekit.desktop.tmp',
       configuration: cdata
     ),
    output: 'gnome-software-local-file-packagekit.desktop',
    type: 'desktop',
    po_dir: join_paths(meson.project_source_root(), 'po'),
    install: true,
    install_dir: join_paths(get_option('datadir'), 'applications')
  )
endif

if get_option('snap')
  i18n.merge_file(
    input:
     configure_file(
       input: 'gnome-software-local-file-snap.desktop.in',
       output: 'gnome-software-local-file-snap.desktop.tmp',
       configuration: cdata
     ),
    output: 'gnome-software-local-file-snap.desktop',
    type: 'desktop',
    po_dir: join_paths(meson.project_source_root(), 'po'),
    install: true,
    install_dir: join_paths(get_option('datadir'), 'applications')
  )
endif

configure_file(
    input: 'gnome-software.service.in',
    output: 'gnome-software.service',
    configuration: cdata,
    install: true,
    install_dir: systemd_user_unit_dir,
)

install_data('org.gnome.Software-search-provider.ini',
             install_dir: 'share/gnome-shell/search-providers')

if get_option('man')
  xsltproc = find_program('xsltproc')
  custom_target('manfile-gnome-software',
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

if get_option('packagekit')
  # replace @bindir@
  configure_file(
    input: 'org.freedesktop.PackageKit.service.in',
    output: 'org.freedesktop.PackageKit.service',
    install_dir: join_paths(get_option('datadir'), 'dbus-1', 'services'),
    configuration: cdata
  )
endif

if get_option('tests')
  test_env = environment()
  test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
  test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

  cargs += ['-DTESTDATADIR="' + join_paths(meson.current_source_dir(), '..', 'data') + '"']
  e = executable(
    'gs-self-test-src',
    compiled_schemas,
    sources: [
      'gs-css.c',
      'gs-common.c',
      'gs-self-test.c',
    ],
    include_directories: [
      include_directories('..'),
      include_directories('../lib'),
    ],
    dependencies: [
      appstream,
      gio_unix,
      glib,
      gmodule,
      gtk,
      json_glib,
      libadwaita,
      libgnomesoftware_dep,
      libm,
      libsoup,
    ],
    c_args: cargs
  )
  test('gs-self-test-src', e, suite: ['plugins', 'src'], env: test_env)
endif
