cargs = ['-DG_LOG_DOMAIN="Gs"']
cargs += ['-DLOCALPLUGINDIR=""']

configure_file(
  input : 'gnome-software.pc.in',
  output : 'gnome-software.pc',
  install: true,
  install_dir: join_paths(get_option('libdir'), 'pkgconfig'),
  configuration : conf
)

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

libgnomesoftware = static_library(
  'libgnomesoftware',
  sources : [
    'gs-app.c',
    'gs-app-list.c',
    'gs-auth.c',
    'gs-category.c',
    'gs-debug.c',
    'gs-os-release.c',
    'gs-plugin.c',
    'gs-plugin-event.c',
    'gs-plugin-loader.c',
    'gs-plugin-loader-sync.c',
    'gs-test.c',
    'gs-utils.c',
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
    libsoup,
    valgrind,
  ],
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
    gtk,
    json_glib,
    libm,
    libsecret,
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

if get_option('enable-tests')
  cargs += ['-DTESTDATADIR="' + join_paths(meson.current_source_dir(), '..', 'data') + '"']
  e = executable(
    'gs-self-test',
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
      gtk,
      json_glib,
      libm,
      libsecret,
      libsoup
    ],
    link_with : [
      libgnomesoftware
    ],
    c_args : cargs
  )
  test('gs-self-test-lib', e)
endif
