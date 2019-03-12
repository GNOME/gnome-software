c_args = ['-DG_LOG_DOMAIN="GsPluginParentalControls"']

shared_module(
  'gs_plugin_parental_controls',
  sources : 'gs-plugin-parental-controls.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : c_args,
  dependencies : [ plugin_libs, malcontent ],
  link_with : [
    libgnomesoftware
  ],
)
metainfo = 'org.gnome.Software.Plugin.ParentalControls.metainfo.xml'

i18n.merge_file(
  input: metainfo + '.in',
  output: metainfo,
  type: 'xml',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'metainfo'),
)
