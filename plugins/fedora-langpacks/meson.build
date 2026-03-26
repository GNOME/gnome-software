cargs = ['-DG_LOG_DOMAIN="GsPluginFedoraLangpacks"']

gs_plugin_fedora_langpacks = shared_module(
  'gs_plugin_fedora-langpacks',
  sources : 'gs-plugin-fedora-langpacks.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
)

subdir('tests')
