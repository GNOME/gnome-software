cargs = ['-DG_LOG_DOMAIN="GsPluginFedoraTaggerUsage"']


shared_module(
  'gs_plugin_fedora-tagger-usage',
  sources : 'gs-plugin-fedora-tagger-usage.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs
)
