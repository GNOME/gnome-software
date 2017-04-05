cargs = ['-DG_LOG_DOMAIN="GsPluginUbuntuOne"']

shared_module(
  'gs_plugin_ubuntuone',
  sources : 'gs-plugin-ubuntuone.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs
)
