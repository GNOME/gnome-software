# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2026 Philip Withnall
# Authors: Philip Withnall <pwithnall@gnome.org>

# The DynamicLauncher interface xml comes from:
# https://github.com/flatpak/xdg-desktop-portal/blob/main/data/org.freedesktop.portal.DynamicLauncher.xml
gdbus_codegen = find_program('gdbus-codegen')
dynamic_launcher_portal_iface_h = custom_target(
  'gs-dynamic-launcher-portal-iface.h',
  input: ['org.freedesktop.portal.DynamicLauncher.xml'],
  output: ['gs-dynamic-launcher-portal-iface.h'],
  command: [gdbus_codegen,
            '--interface-info-header',
            '--output', '@OUTPUT@',
            '@INPUT@'],
)
dynamic_launcher_portal_iface_c = custom_target(
  'gs-dynamic-launcher-portal-iface.c',
  input: ['org.freedesktop.portal.DynamicLauncher.xml'],
  output: ['gs-dynamic-launcher-portal-iface.c'],
  command: [gdbus_codegen,
            '--interface-info-body',
            '--output', '@OUTPUT@',
            '@INPUT@'],
)

epiphany_tests = {
  'epiphany': {
    'source': [
      'epiphany.c',
      dynamic_launcher_portal_iface_c,
      dynamic_launcher_portal_iface_h,
      epiphany_generated,
    ],
  },
}

test_env = environment()
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

test_deps = [
  plugin_libs,
  dependency('glib-testing-0', fallback: ['libglib-testing', 'libglib_testing_dep']),
]
test_cargs = [
  '-DLOCALPLUGINDIR="' + meson.current_build_dir() + '/../"',
  '-DLOCALPLUGINDIR_CORE="' + meson.current_build_dir() + '/../../core"',
  '-DTESTDATADIR="' + join_paths(meson.project_source_root(), 'data') + '"',
]
test_cpp_args = ['-DG_LOG_DOMAIN="epiphany"']

foreach test_name, extra_args : epiphany_tests
  source = extra_args.get('source', test_name + '.c')

  if installed_tests_enabled
    test_conf = configuration_data()
    test_conf.set('installed_tests_dir', installed_tests_execdir)
    test_conf.set('program', test_name)
    configure_file(
      input: installed_tests_template,
      output: test_name + '.test',
      install_dir: installed_tests_metadir,
      install_tag: 'tests',
      configuration: test_conf,
    )
  endif

  exe = executable(test_name, source,
    c_args: test_cargs,
    cpp_args: test_cpp_args,
    dependencies: test_deps,
    install_dir: installed_tests_execdir,
    install_tag: 'tests',
    install: installed_tests_enabled,
    install_rpath: gs_private_libdir,
  )

  test(test_name, exe,
    protocol: 'tap',
    env: test_env,
    timeout: 30,
    depends: [compiled_schemas],
    suite: ['plugins', 'epiphany'],
  )
endforeach
