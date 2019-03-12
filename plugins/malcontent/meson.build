c_args = ['-DG_LOG_DOMAIN="GsPluginParentalControls"']

shared_module(
  'gs_plugin_parental_controls',
  sources : 'gs-plugin-parental-controls.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : c_args,
  dependencies : [ plugin_libs, malcontent ],
  link_with : [
    libgnomesoftware
  ],
)