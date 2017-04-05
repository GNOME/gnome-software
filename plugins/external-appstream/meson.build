cargs = ['-DG_LOG_DOMAIN="GsPluginExternalAppstream"']

executable(
  'gnome-software-install-appstream',
  sources : 'gs-install-appstream.c',
  include_directories : [
    include_directories('@0@/..'.format(meson.current_build_dir())),
  ],
  dependencies : [appstream_glib],
  c_args : cargs,
  install : true,
  install_dir : 'libexec'
)

shared_module(
  'gs_plugin_external-appstream',
sources : 'gs-plugin-external-appstream.c',
  include_directories : [
    include_directories('../..'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [gio_unix, appstream_glib, libsoup]
)
