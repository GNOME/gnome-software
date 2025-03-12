cargs = ['-DG_LOG_DOMAIN="GsPluginCore"']

shared_module(
  'gs_plugin_generic-updates',
  sources : 'gs-plugin-generic-updates.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
)

shared_module(
  'gs_plugin_provenance',
  sources : 'gs-plugin-provenance.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
)

shared_module(
  'gs_plugin_provenance-license',
  sources : 'gs-plugin-provenance-license.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
)


shared_module(
  'gs_plugin_icons',
  sources : 'gs-plugin-icons.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
)

shared_module(
  'gs_plugin_appstream',
  sources : [
    'gs-plugin-appstream.c'
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_libs,
    libxmlb,
  ],
)

shared_module(
  'gs_plugin_hardcoded-blocklist',
  sources : 'gs-plugin-hardcoded-blocklist.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
)

shared_module(
  'gs_plugin_os-release',
  sources : 'gs-plugin-os-release.c',
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
    'gs-self-test-core',
    compiled_schemas,
    sources : [
      'gs-self-test.c',
    ],
    dependencies : [
      plugin_libs,
      libxmlb,
    ],
    c_args : cargs,
  )
  test('gs-self-test-core', e, suite: ['plugins', 'core'], env: test_env)
endif
