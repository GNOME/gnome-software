cargs = ['-DG_LOG_DOMAIN="GsPluginLimba"']

if get_option('enable-limba')
shared_module(
  'gs_plugin_limba',
  sources : 'gs-plugin-limba.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, limba ]
  )
endif
