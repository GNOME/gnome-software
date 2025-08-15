cargs = ['-DG_LOG_DOMAIN="GsPluginRpmOstree"']

rpmostree_generated = gnome.gdbus_codegen(
  'gs-rpmostree-generated',
  'org.projectatomic.rpmostree1.xml',
  interface_prefix : 'org.projectatomic.rpmostree1',
  namespace : 'GsRPMOSTree',
  extra_args : [ '--glib-min-required=' + glib.version() ],
)

shared_module(
  'gs_plugin_rpm-ostree',
  rpmostree_generated,
  sources : [
    'gs-plugin-rpm-ostree.c',
    'gs-rpm-ostree-utils.c',
  ],
  install : true,
  install_dir: plugin_dir,
  install_rpath: join_paths(rpm_ostree.get_variable('libdir'), 'rpm-ostree'),
  c_args : cargs,
  dependencies : [ plugin_libs, ostree, rpm, rpm_ostree ],
)

if get_option('tests')
  test_env = environment()
  test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
  test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

  cargs += ['-DTESTDATADIR="' + join_paths(meson.current_source_dir(), 'tests') + '"']
  e = executable(
    'gs-self-test-rpm-ostree',
    compiled_schemas,
    sources : [
      'gs-rpm-ostree-utils.c',
      'gs-self-test.c',
    ],
    dependencies : [
      plugin_libs,
    ],
    c_args : cargs,
  )
  test('gs-self-test-rpm-ostree', e, suite: ['plugins', 'rpm-ostree'], env: test_env)
endif
