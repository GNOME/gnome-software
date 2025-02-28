cargs = ['-DG_LOG_DOMAIN="GsPluginModalias"']
cargs += ['-DLOCALPLUGINDIR="' + meson.current_build_dir() + '"']
cargs += ['-DLOCALPLUGINDIR_CORE="' + meson.current_build_dir() + '/../core"']
cargs += ['-DLOCALPLUGINDIR_DUMMY="' + meson.current_build_dir() + '/../dummy"']

shared_module(
  'gs_plugin_modalias',
  sources : 'gs-plugin-modalias.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, gudev ],
)

if get_option('tests')
  test_env = environment()
  test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
  test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

  e = executable(
    'gs-self-test-modalias',
    compiled_schemas,
    sources : [
      'gs-self-test.c'
    ],
    dependencies : [
      plugin_libs,
    ],
    c_args : cargs,
  )
  test('gs-self-test-modalias', e, suite: ['plugins', 'modalias'], env: test_env)
endif
