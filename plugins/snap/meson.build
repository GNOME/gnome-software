cargs = ['-DG_LOG_DOMAIN="GsPluginSnap"']

gs_plugin_snap = shared_module(
  'gs_plugin_snap',
  sources : [
    'gs-plugin-snap.c'
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, snap ],
)
metainfo = 'org.gnome.Software.Plugin.Snap.metainfo.xml'

i18n.merge_file(
  input: metainfo + '.in',
  output: metainfo,
  type: 'xml',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'metainfo')
)

subdir('tests')
