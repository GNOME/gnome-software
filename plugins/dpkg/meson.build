cargs = ['-DG_LOG_DOMAIN="GsPluginDpkg"']

shared_module(
  'gs_plugin_dpkg',
  sources : 'gs-plugin-dpkg.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
)

if get_option('tests')
  test_env = environment()
  test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
  test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

  cargs += ['-DLOCALPLUGINDIR="' + meson.current_build_dir() + '"']
  cargs += ['-DTESTDATADIR="' + join_paths(meson.current_source_dir(), 'tests') + '"']
  e = executable(
    'gs-self-test-dpkg',
    compiled_schemas,
    sources : [
      'gs-self-test.c'
    ],
    dependencies : [
      plugin_libs,
    ],
    c_args : cargs,
  )
  test('gs-self-test-dpkg', e, suite: ['plugins', 'dpkg'], env: test_env)
endif
