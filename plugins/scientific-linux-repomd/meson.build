cargs = ['-DG_LOG_DOMAIN="GsPluginScientificLinuxRepomd"']

shared_module(
  'gs_plugin_scientific-linux-repomd',
  sources : 'gs-plugin-scientific-linux-repomd.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
)
