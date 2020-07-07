cargs = ['-DG_LOG_DOMAIN="Gs"']
cargs += ['-DLOCALPLUGINDIR=""']

install_headers([
    'gnome-software.h',
    'gs-app.h',
    'gs-app-collation.h',
    'gs-app-list.h',
    'gs-autocleanups.h',
    'gs-category.h',
    'gs-ioprio.h',
    'gs-metered.h',
    'gs-os-release.h',
    'gs-plugin.h',
    'gs-plugin-event.h',
    'gs-plugin-job.h',
    'gs-plugin-loader.h',
    'gs-plugin-loader-sync.h',
    'gs-plugin-types.h',
    'gs-plugin-vfuncs.h',
    'gs-utils.h'
  ],
  subdir : 'gnome-software'
)

librarydeps = [
  appstream_glib,
  gio_unix,
  glib,
  gmodule,
  goa,
  gtk,
  json_glib,
  libm,
  libsoup,
  libsysprof_capture_dep,
  valgrind,
]

if get_option('mogwai')
  librarydeps += mogwai_schedule_client
endif

if get_option('polkit')
  librarydeps += polkit
endif

libgnomesoftware = static_library(
  'gnomesoftware',
  sources : [
    'gs-app.c',
    'gs-app-list.c',
    'gs-category.c',
    'gs-debug.c',
    'gs-ioprio.c',
    'gs-ioprio.h',
    'gs-metered.c',
    'gs-os-release.c',
    'gs-plugin.c',
    'gs-plugin-event.c',
    'gs-plugin-job.c',
    'gs-plugin-loader.c',
    'gs-plugin-loader-sync.c',
    'gs-test.c',
    'gs-utils.c',
  ],
  include_directories : [
    include_directories('..'),
  ],
  dependencies : librarydeps,
  c_args : cargs,
  install: true,
)

pkg = import('pkgconfig')

pkg.generate(
    libgnomesoftware,
    description : 'GNOME Software is a software center for GNOME',
    filebase : 'gnome-software',
    name : 'gnome-software',
    subdirs : 'gnome-software',
    variables : 'plugindir=${libdir}/gs-plugins-' + gs_plugin_api_version,
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
    appstream_glib,
    gio_unix,
    glib,
    gmodule,
    goa,
    gtk,
    json_glib,
    libm,
    libsoup,
    valgrind,
  ],
  link_with : [
    libgnomesoftware
  ],
  c_args : cargs,
  install : true,
  install_dir : get_option('libexecdir')
)

if get_option('tests')
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
      appstream_glib,
      gio_unix,
      glib,
      gmodule,
      goa,
      gtk,
      json_glib,
      libm,
      libsoup
    ],
    link_with : [
      libgnomesoftware
    ],
    c_args : cargs
  )
  test('gs-self-test-lib', e, suite: ['lib'], env: test_env, timeout : 120)
endif
