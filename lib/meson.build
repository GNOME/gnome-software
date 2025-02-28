cargs = ['-DG_LOG_DOMAIN="Gs"']
cargs += ['-DLOCALPLUGINDIR=""']

libgnomesoftware_public_headers = [
  'gnome-software.h',
  'gs-app.h',
  'gs-app-collation.h',
  'gs-app-list.h',
  'gs-app-permissions.h',
  'gs-app-query.h',
  'gs-appstream.h',
  'gs-category.h',
  'gs-category-manager.h',
  'gs-desktop-data.h',
  'gs-download-utils.h',
  'gs-external-appstream-utils.h',
  'gs-icon.h',
  'gs-icon-downloader.h',
  'gs-ioprio.h',
  'gs-job-manager.h',
  'gs-key-colors.h',
  'gs-metered.h',
  'gs-odrs-provider.h',
  'gs-os-release.h',
  'gs-plugin.h',
  'gs-plugin-event.h',
  'gs-plugin-helpers.h',
  'gs-plugin-job.h',
  'gs-plugin-job-cancel-offline-update.h',
  'gs-plugin-job-download-upgrade.h',
  'gs-plugin-job-file-to-app.h',
  'gs-plugin-job-launch.h',
  'gs-plugin-job-list-apps.h',
  'gs-plugin-job-list-categories.h',
  'gs-plugin-job-list-distro-upgrades.h',
  'gs-plugin-job-manage-repository.h',
  'gs-plugin-job-refine.h',
  'gs-plugin-job-refresh-metadata.h',
  'gs-plugin-job-trigger-upgrade.h',
  'gs-plugin-job-install-apps.h',
  'gs-plugin-job-uninstall-apps.h',
  'gs-plugin-job-update-apps.h',
  'gs-plugin-job-url-to-app.h',
  'gs-plugin-loader.h',
  'gs-plugin-loader-sync.h',
  'gs-plugin-types.h',
  'gs-plugin-vfuncs.h',
  'gs-remote-icon.h',
  'gs-rewrite-resources.h',
  'gs-test.h',
  'gs-utils.h',
  'gs-worker-thread.h',
]

install_headers(libgnomesoftware_public_headers,
  subdir : 'gnome-software'
)

librarydeps = [
  appstream,
  gio_unix,
  glib,
  gmodule,
  gtk,
  json_glib,
  libm,
  libsoup,
  libsysprof_capture_dep,
  libxmlb,
]

if get_option('mogwai')
  librarydeps += mogwai_schedule_client
endif

if get_option('polkit')
  librarydeps += polkit
endif

dkms_private_files = []
if get_option('dkms')
   dkms_private_files = [ 'gs-dkms-private.h', 'gs-dkms-private.c' ]
endif

gs_build_ident_h = vcs_tag(
     fallback: meson.project_version(),
        input: 'gs-build-ident.h.in',
       output: 'gs-build-ident.h',
)

libgnomesoftware_enums = gnome.mkenums_simple('gs-enums',
  sources : libgnomesoftware_public_headers,
  install_header : true,
  install_dir : join_paths(get_option('includedir'), 'gnome-software'),
)

libgnomesoftware_include_directories = [
  include_directories('..'),
  include_directories('.'),
]

libgnomesoftware_generated_sources = libgnomesoftware_enums + [gs_build_ident_h]

libgnomesoftware = library(
  'gnomesoftware',
  sources : [
    'gs-app.c',
    'gs-app-list.c',
    'gs-app-permissions.c',
    'gs-app-query.c',
    'gs-appstream.c',
    'gs-category.c',
    'gs-category-manager.c',
    'gs-debug.c',
    'gs-desktop-data.c',
    'gs-download-utils.c',
    'gs-external-appstream-utils.c',
    'gs-fedora-third-party.c',
    'gs-icon.c',
    'gs-icon-downloader.c',
    'gs-ioprio.c',
    'gs-ioprio.h',
    'gs-job-manager.c',
    'gs-key-colors.c',
    'gs-metered.c',
    'gs-odrs-provider.c',
    'gs-os-release.c',
    'gs-plugin.c',
    'gs-plugin-event.c',
    'gs-plugin-helpers.c',
    'gs-plugin-job.c',
    'gs-plugin-job-cancel-offline-update.c',
    'gs-plugin-job-download-upgrade.c',
    'gs-plugin-job-file-to-app.c',
    'gs-plugin-job-launch.c',
    'gs-plugin-job-list-apps.c',
    'gs-plugin-job-list-categories.c',
    'gs-plugin-job-list-distro-upgrades.c',
    'gs-plugin-job-manage-repository.c',
    'gs-plugin-job-refine.c',
    'gs-plugin-job-refresh-metadata.c',
    'gs-plugin-job-trigger-upgrade.c',
    'gs-plugin-job-install-apps.c',
    'gs-plugin-job-uninstall-apps.c',
    'gs-plugin-job-update-apps.c',
    'gs-plugin-job-url-to-app.c',
    'gs-plugin-loader.c',
    'gs-plugin-loader-sync.c',
    'gs-profiler.h',
    'gs-remote-icon.c',
    'gs-rewrite-resources.c',
    'gs-test.c',
    'gs-utils.c',
    'gs-worker-thread.c',
  ] + libgnomesoftware_generated_sources + dkms_private_files,
  soversion: gs_plugin_api_version,
  include_directories : libgnomesoftware_include_directories,
  dependencies : librarydeps,
  c_args : cargs,
  install: true,
  install_dir: gs_private_libdir,
)

libgnomesoftware_dep = declare_dependency(link_with : libgnomesoftware,
  sources : libgnomesoftware_generated_sources,
  include_directories : libgnomesoftware_include_directories,
  dependencies: librarydeps,
)

pkg = import('pkgconfig')

pkg.generate(
    libgnomesoftware,
    description : 'GNOME Software is a software center for GNOME',
    filebase : 'gnome-software',
    name : 'gnome-software',
    subdirs : 'gnome-software',
    variables : [
      'gs_private_libdir=${libdir}/gnome-software',
      'plugindir=${gs_private_libdir}/plugins-' + gs_plugin_api_version,
      'soupapiversion=3.0' # This is here for any out-of-tree plugins
                           # depending on it, and should be removed in
                           # future.
    ],
    install_dir : join_paths(get_option('prefix'), get_option('libdir'), 'pkgconfig'),  # or it defaults to gs_private_libdir, which is wrong
)

executable(
  'gnome-software-cmd',
  sources : [
    'gs-cmd.c',
  ],
  include_directories : [
    include_directories('..'),
  ],
  dependencies : [
    appstream,
    gio_unix,
    glib,
    gmodule,
    gtk,
    json_glib,
    libgnomesoftware_dep,
    libm,
    libsoup,
  ],
  c_args : cargs,
  install : true,
  install_dir : get_option('libexecdir'),
  install_rpath : gs_private_libdir,
)

if get_option('tests')
  test_env = environment()
  test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
  test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

  cargs += ['-DTESTDATADIR="' + join_paths(meson.current_source_dir(), '..', 'data') + '"']
  e = executable(
    'gs-self-test',
    compiled_schemas,
    sources : [
      'gs-self-test.c'
    ],
    include_directories : [
      include_directories('..'),
    ],
    dependencies : [
      appstream,
      gio_unix,
      glib,
      gmodule,
      gtk,
      json_glib,
      libgnomesoftware_dep,
      libm,
      libsoup
    ],
    c_args : cargs
  )
  test('gs-self-test-lib', e, suite: ['lib'], env: test_env, timeout : 120)
endif

subdir('tests')
subdir('tools')
