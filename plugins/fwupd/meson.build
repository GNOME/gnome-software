cargs = ['-DG_LOG_DOMAIN="GsPluginFwupd"']
cargs += ['-DLOCALPLUGINDIR="' + meson.current_build_dir() + '"']
deps = [
  plugin_libs,
  fwupd,
]

if get_option('mogwai')
  deps += [mogwai_schedule_client]
endif

shared_module(
  'gs_plugin_fwupd',
  sources : [
    'gs-fwupd-app.c',
    'gs-plugin-fwupd.c',
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : deps,
)
metainfo = 'org.gnome.Software.Plugin.Fwupd.metainfo.xml'

i18n.merge_file(
  input: metainfo + '.in',
  output: metainfo,
  type: 'xml',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'metainfo')
)

if get_option('tests')
  test_env = environment()
  test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
  test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

  cargs += ['-DTESTDATADIR="' + join_paths(meson.current_source_dir(), 'tests') + '"']
  e = executable(
    'gs-self-test-fwupd',
    compiled_schemas,
    sources : [
      'gs-self-test.c'
    ],
    dependencies : deps,
    c_args : cargs,
  )
  test('gs-self-test-fwupd', e, suite: ['plugins', 'fwupd'], env: test_env)
endif
