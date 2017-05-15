cargs = ['-DG_LOG_DOMAIN="GsPluginSteam"']

shared_module(
  'gs_plugin_steam',
  sources : 'gs-plugin-steam.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs
)
metainfo = 'org.gnome.Software.Plugin.Steam.metainfo.xml'

i18n.merge_file(
  input: metainfo + '.in',
  output: metainfo,
  type: 'xml',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'metainfo')
)
