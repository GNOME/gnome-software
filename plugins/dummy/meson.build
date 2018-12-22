cargs = ['-DG_LOG_DOMAIN="GsPluginDummy"']
cargs += ['-DLOCALPLUGINDIR="' + meson.current_build_dir() + '"']
cargs += ['-DLOCALPLUGINDIR_CORE="' + meson.current_build_dir() + '/../core"']

shared_module(
  'gs_plugin_dummy',
  sources : 'gs-plugin-dummy.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [appstream_glib, gio_unix, goa, gtk, libsoup],
  link_with : [
    libgnomesoftware
  ]
)

if get_option('tests')
  e = executable(
    'gs-self-test-dummy',
    compiled_schemas,
    sources : [
      'gs-self-test.c'
    ],
    include_directories : [
      include_directories('../..'),
      include_directories('../../lib'),
    ],
    dependencies : [
      plugin_libs,
    ],
    link_with : [
      libgnomesoftware
    ],
    c_args : cargs,
  )
  test('gs-self-test-dummy', e, env: test_env)
endif
