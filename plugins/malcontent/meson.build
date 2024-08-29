c_args = ['-DG_LOG_DOMAIN="GsPluginMalcontent"']

shared_module(
  'gs_plugin_malcontent',
  sources : 'gs-plugin-malcontent.c',
  install : true,
  install_dir: plugin_dir,
  c_args : c_args,
  dependencies : [ plugin_libs, malcontent ],
)
