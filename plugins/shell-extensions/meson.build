cargs = ['-DG_LOG_DOMAIN="GsPluginShellExtensions"']
cargs += ['-DLOCALPLUGINDIR="' + meson.current_build_dir() + '"']

shared_module(
  'gs_plugin_shell-extensions',
  sources : [
    'gs-appstream.c',
    'gs-plugin-shell-extensions.c'
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_libs,
    libxmlb,
  ],
  link_with : [
    libgnomesoftware
  ]
)

if get_option('tests')
  e = executable(
    'gs-self-test-shell-extensions',
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
      libxmlb,
    ],
    link_with : [
      libgnomesoftware
    ],
    c_args : cargs,
  )
  test('gs-self-test-shell-extensions', e, env: test_env)
endif
