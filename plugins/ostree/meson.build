cargs = ['-DG_LOG_DOMAIN="GsPluginOstree"']

shared_module(
  'gs_plugin_ostree',
  sources : 'gs-plugin-ostree.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, ostree ]
)

if get_option('enable-rpm')
  shared_module(
    'gs_plugin_rpm',
    sources : 'gs-plugin-rpm.c',
    include_directories : [
      include_directories('../..'),
      include_directories('../../lib'),
    ],
    install : true,
    install_dir: plugin_dir,
    c_args : cargs,
    dependencies : [ plugin_libs, rpm ]
  )
endif
