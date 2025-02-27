cargs = ['-DG_LOG_DOMAIN="GsPluginDummy"']
cargs += ['-DLOCALPLUGINDIR="' + meson.current_build_dir() + '"']
cargs += ['-DLOCALPLUGINDIR_CORE="' + meson.current_build_dir() + '/../core"']

shared_module(
  'gs_plugin_dummy',
  sources : 'gs-plugin-dummy.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [plugin_libs],
)

if get_option('tests')
  test_env = environment()
  test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
  test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

  resources_src = gnome.compile_resources(
    'gs-resources',
    '../../src/gnome-software.gresource.xml',
    source_dir : '../../src',
    c_name : 'gs'
  )

  e = executable(
    'gs-self-test-dummy',
    compiled_schemas,
    resources_src,
    sources : [
      'gs-self-test.c'
    ],
    dependencies : [
      plugin_libs,
    ],
    c_args : cargs,
  )
  test('gs-self-test-dummy', e, suite: ['plugins', 'dummy'], env: test_env)
endif
