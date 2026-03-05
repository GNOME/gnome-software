cargs = ['-DG_LOG_DOMAIN="GsPluginDpkg"']

gs_plugin_dpkg = shared_module(
  'gs_plugin_dpkg',
  sources : 'gs-plugin-dpkg.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
)

subdir('tests')