cargs = ['-DG_LOG_DOMAIN="GsPluginFwupd"']
deps = [
  plugin_libs,
  fwupd,
]

if get_option('mogwai')
  deps += [mogwai_schedule_client]
endif

shared_module(
  'gs_plugin_fwupd',
  sources : [
    'gs-fwupd-app.c',
    'gs-plugin-fwupd.c',
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : deps,
)
metainfo = 'org.gnome.Software.Plugin.Fwupd.metainfo.xml'

i18n.merge_file(
  input: metainfo + '.in',
  output: metainfo,
  type: 'xml',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'metainfo')
)

subdir('tests')