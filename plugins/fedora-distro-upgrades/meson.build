cargs = ['-DG_LOG_DOMAIN="GsPluginFedoraDistroUpgrades"']

shared_module(
  'gs_plugin_fedora-distro-upgrades',
  sources : 'gs-plugin-fedora-distro-upgrades.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs
)
