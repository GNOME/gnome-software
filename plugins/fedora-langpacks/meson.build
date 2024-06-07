cargs = ['-DG_LOG_DOMAIN="GsPluginFedoraLangpacks"']

shared_module(
  'gs_plugin_fedora-langpacks',
  sources : 'gs-plugin-fedora-langpacks.c',
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
  cargs += ['-DLOCALPLUGINDIR_PACKAGEKIT="' + meson.current_build_dir() + '/../packagekit"']
  e = executable(
    'gs-self-test-fedora-langpacks',
    compiled_schemas,
    sources : [
      'gs-self-test.c',
    ],
    dependencies : [
      plugin_libs,
    ],
    c_args : cargs,
  )
  test('gs-self-test-fedora-langpacks', e, suite: ['plugins', 'fedora-langpacks'], env: test_env)
endif
