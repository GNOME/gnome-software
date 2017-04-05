cargs = ['-DG_LOG_DOMAIN="GsPluginUbuntuReviews"']

shared_module(
  'gs_plugin_ubuntu-reviews',
  sources : 'gs-plugin-ubuntu-reviews.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, oauth ]
)
