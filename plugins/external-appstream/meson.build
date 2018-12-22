cargs = ['-DG_LOG_DOMAIN="GsPluginExternalAppstream"']

executable(
  'gnome-software-install-appstream',
  sources : [
    'gs-external-appstream-utils.c',
    'gs-install-appstream.c',
  ],
  include_directories : [
    include_directories('../..'),
  ],
  dependencies : [
    gio_unix,
    libxmlb,
  ],
  c_args : cargs,
  install : true,
  install_dir : get_option('libexecdir')
)

shared_module(
  'gs_plugin_external-appstream',
  sources : [
    'gs-external-appstream-utils.c',
    'gs-plugin-external-appstream.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [gio_unix, appstream_glib, libsoup, plugin_libs],
  link_with : [
    libgnomesoftware
  ]
)
