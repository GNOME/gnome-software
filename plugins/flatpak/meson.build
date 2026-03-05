cargs = ['-DG_LOG_DOMAIN="GsPluginFlatpak"']
deps = [
  plugin_libs,
  flatpak,
  libxmlb,
  ostree,
]

if get_option('mogwai')
  deps += mogwai_schedule_client
endif

gs_plugin_flatpak = shared_module(
  'gs_plugin_flatpak',
  sources : [
    'gs-flatpak-app.c',
    'gs-flatpak.c',
    'gs-flatpak-transaction.c',
    'gs-flatpak-utils.c',
    'gs-plugin-flatpak.c'
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : deps,
)
metainfo = 'org.gnome.Software.Plugin.Flatpak.metainfo.xml'

i18n.merge_file(
  input: metainfo + '.in',
  output: metainfo,
  type: 'xml',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'metainfo')
)

subdir('tests')