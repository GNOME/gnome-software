cargs = ['-DG_LOG_DOMAIN="Gs"']
cargs += ['-DLOCALPLUGINDIR=""']

# we can't use conf as this is set_quoted()

pkconf = configuration_data()
pkconf.set('prefix', get_option('prefix'))
pkconf.set('libdir',
           join_paths(get_option('prefix'),
                      get_option('libdir')))
pkconf.set('includedir',
           join_paths(get_option('prefix'),
                      get_option('includedir')))
pkconf.set('GS_PLUGIN_API_VERSION', gs_plugin_api_version)
pkconf.set('VERSION', meson.project_version())
configure_file(
  input : 'gnome-software.pc.in',
  output : 'gnome-software.pc',
  install: true,
  install_dir: join_paths(get_option('libdir'), 'pkgconfig'),
  configuration : pkconf
)

# XXX: This can probably be used (after testing), once
# https://github.com/mesonbuild/meson/pull/1688 is merged
#pkg = import('pkgconfig')
#
#pkg.generate(
#    description : 'GNOME Software is a software center for GNOME',
#    filebase : 'gnome-software',
#    name : 'gnome-software',
#    requires: [ 'gobject-2.0', 'gdk-3.0', 'appstream-glib', 'libsoup-2.4', 'gio-unix-2.0'],
#    requires_private : [ 'gthread-2.0', 'atk' ],
#    subdirs : [ 'gnome-software' ],
#    variables : [ 'plugindir=${libdir}/lib/gs-plugins-' + gs_plugin_api_version ]
#    version : meson.project_version()
#)

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
    'gs-price.h',
    'gs-utils.h'
  ],
  subdir : 'gnome-software'
)

librarydeps = [
  appstream_glib,
  gio_unix,
  gmodule,
  goa,
  gtk,
  json_glib,
  libm,
  libsoup,
  valgrind,
]

if get_option('polkit')
  librarydeps += polkit
endif

libgnomesoftware = static_library(
  'libgnomesoftware',
  sources : [
    'gs-app.c',
    'gs-app-list.c',
    'gs-auth.c',
    'gs-category.c',
    'gs-debug.c',
    'gs-ioprio.c',
    'gs-ioprio.h',
    'gs-os-release.c',
    'gs-plugin.c',
    'gs-plugin-event.c',
    'gs-plugin-job.c',
    'gs-plugin-loader.c',
    'gs-plugin-loader-sync.c',
    'gs-price.c',
    'gs-test.c',
    'gs-utils.c',
  ],
  include_directories : [
    include_directories('..'),
  ],
  dependencies : librarydeps,
  c_args : cargs,
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
  test('gs-self-test-lib', e, env: test_env, timeout : 120)
endif
