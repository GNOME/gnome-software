cargs = ['-DG_LOG_DOMAIN="GsPluginModalias"']

gs_plugin_modalias = shared_module(
  'gs_plugin_modalias',
  sources : 'gs-plugin-modalias.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, gudev ],
)

subdir('tests')