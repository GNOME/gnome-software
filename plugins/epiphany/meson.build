cargs = ['-DG_LOG_DOMAIN="GsPluginEpiphany"']

# The WebAppProvider interface xml comes from:
# https://gitlab.gnome.org/GNOME/epiphany/-/blob/master/src/webapp-provider/org.gnome.Epiphany.WebAppProvider.xml
epiphany_generated = gnome.gdbus_codegen(
  'gs-epiphany-generated',
  sources : ['org.gnome.Epiphany.WebAppProvider.xml'],
  interface_prefix : 'org.gnome.Epiphany',
  namespace : 'GsEphy',
)

shared_module(
  'gs_plugin_epiphany',
  epiphany_generated,
  sources : 'gs-plugin-epiphany.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
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
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'metainfo'),
)

if get_option('tests')
  cargs += ['-DLOCALPLUGINDIR="' + meson.current_build_dir() + '"']
  cargs += ['-DLOCALPLUGINDIR_CORE="' + meson.current_build_dir() + '/../core"']
  cargs += ['-DTESTDATADIR="' + join_paths(meson.current_source_dir(), '..', '..', 'data') + '"']
  e = executable(
    'gs-self-test-epiphany',
    compiled_schemas,
    sources : [
      'gs-self-test.c',
    ],
    include_directories : [
      include_directories('../..'),
      include_directories('../../lib'),
    ],
    dependencies : [
      plugin_libs,
    ],
    link_with : [
      libgnomesoftware,
    ],
    c_args : cargs,
  )
  test('gs-self-test-epiphany', e, suite: ['plugins', 'epiphany'], env: test_env)
endif
