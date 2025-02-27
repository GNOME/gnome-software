cargs = ['-DG_LOG_DOMAIN="GsPluginSnap"']

shared_module(
  'gs_plugin_snap',
  sources : [
    'gs-plugin-snap.c'
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, snap ],
)
metainfo = 'org.gnome.Software.Plugin.Snap.metainfo.xml'

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

  cargs += ['-DLOCALPLUGINDIR="' + meson.current_build_dir() + '"']
  cargs += ['-DLOCALPLUGINDIR_CORE="' + meson.current_build_dir() + '/../core"']
  e = executable(
    'gs-self-test-snap',
    compiled_schemas,
    sources : [
      'gs-self-test.c'
    ],
    dependencies : [
      plugin_libs,
      snap
    ],
    c_args : cargs,
  )
  test('gs-self-test-snap', e, suite: ['plugins', 'snap'], env: test_env)
endif
