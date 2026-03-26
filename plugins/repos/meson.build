cargs = ['-DG_LOG_DOMAIN="GsPluginRepos"']

gs_plugin_repos = shared_module(
  'gs_plugin_repos',
  sources : 'gs-plugin-repos.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
)

subdir('tests')
