cargs = ['-DG_LOG_DOMAIN="GsPluginFwupd"']
cargs += ['-DLOCALPLUGINDIR="' + meson.current_build_dir() + '"']

shared_module(
  'gs_plugin_fwupd',
  sources : [
    'gs-fwupd-app.c',
    'gs-plugin-fwupd.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, fwupd ]
)
metainfo = 'org.gnome.Software.Plugin.Fwupd.metainfo.xml'

i18n.merge_file(
  input: metainfo + '.in',
  output: metainfo,
  type: 'xml',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'metainfo')
)

if get_option('tests')
  cargs += ['-DTESTDATADIR="' + join_paths(meson.current_source_dir(), 'tests') + '"']
  e = executable(
    'gs-self-test-fwupd',
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
  test('gs-self-test-fwupd', e, env: test_env)
endif
