cargs = ['-DG_LOG_DOMAIN="GsPluginPackageKit"']
cargs += ['-DLOCALPLUGINDIR="' + meson.current_build_dir() + '"']
deps = [
  plugin_libs,
  packagekit,
  gsettings_desktop_schemas,
]

if get_option('mogwai')
  deps += [mogwai_schedule_client]
endif

shared_module(
  'gs_plugin_packagekit',
  sources : [
    'gs-plugin-packagekit.c',
    'gs-packagekit-helper.c',
    'gs-packagekit-task.c',
    'packagekit-common.c',
    'gs-markdown.c',
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : deps,
)

if get_option('tests')
  test_env = environment()
  test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
  test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

  cargs += ['-DTESTDATADIR="' + join_paths(meson.current_source_dir(), 'tests') + '"']
  e = executable(
    'gs-self-test-packagekit',
    compiled_schemas,
    sources : [
      'gs-markdown.c',
      'gs-self-test.c'
    ],
    dependencies : [
      plugin_libs,
    ],
    c_args : cargs,
  )
  test('gs-self-test-packagekit', e, suite: ['plugins', 'packagekit'], env: test_env)
endif
