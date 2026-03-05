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

gs_plugin_epiphany = shared_module(
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

subdir('tests')