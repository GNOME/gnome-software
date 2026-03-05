cargs = ['-DG_LOG_DOMAIN="GsPluginDummy"']

gs_plugin_dummy = shared_module(
  'gs_plugin_dummy',
  sources : 'gs-plugin-dummy.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [plugin_libs],
)

subdir('tests')