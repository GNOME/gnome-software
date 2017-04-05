cargs = ['-DG_LOG_DOMAIN="GsPlugin"']

shared_module(
  'gs_plugin_snap',
  sources : [
    'gs-plugin-snap.c',
    'gs-snapd.c'
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, snap ]
)
metainfo = 'org.gnome.Software.Plugin.Snap.metainfo.xml'

i18n.merge_file(
  input: metainfo + '.in',
  output: metainfo,
  type: 'xml',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'appdata')
)
