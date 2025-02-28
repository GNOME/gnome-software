cargs = ['-DG_LOG_DOMAIN="GsPluginEpiphany"']

# The WebAppProvider interface xml comes from:
# https://gitlab.gnome.org/GNOME/epiphany/-/blob/master/src/webapp-provider/org.gnome.Epiphany.WebAppProvider.xml
epiphany_generated = gnome.gdbus_codegen(
  'gs-epiphany-generated',
  sources : ['org.gnome.Epiphany.WebAppProvider.xml'],
  interface_prefix : 'org.gnome.Epiphany',
  namespace : 'GsEphy',
  extra_args : [ '--glib-min-required=' + glib.version() ],
)

shared_module(
  'gs_plugin_epiphany',
  epiphany_generated,
  sources : 'gs-plugin-epiphany.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
  link_with : [
    libgnomesoftware,
  ],
)
metainfo = 'org.gnome.Software.Plugin.Epiphany.metainfo.xml'

i18n.merge_file(
  input: metainfo + '.in',
  output: metainfo,
  type: 'xml',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'metainfo'),
)

if get_option('tests')
  test_env = environment()
  test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
  test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

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
  cargs += ['-DLOCALPLUGINDIR="' + meson.current_build_dir() + '"']
  cargs += ['-DLOCALPLUGINDIR_CORE="' + meson.current_build_dir() + '/../core"']
  cargs += ['-DTESTDATADIR="' + join_paths(meson.current_source_dir(), '..', '..', 'data') + '"']
  e = executable(
    'gs-self-test-epiphany',
    compiled_schemas,
    sources : [
      'gs-self-test.c',
      dynamic_launcher_portal_iface_c,
      dynamic_launcher_portal_iface_h,
      epiphany_generated,
    ],
    dependencies : [
      plugin_libs,
      dependency('glib-testing-0', fallback: ['libglib-testing', 'libglib_testing_dep']),
    ],
    link_with : [
      libgnomesoftware,
    ],
    c_args : cargs,
  )
  test('gs-self-test-epiphany', e, suite: ['plugins', 'epiphany'], env: test_env)
endif
