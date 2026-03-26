cargs = ['-DG_LOG_DOMAIN="GsPluginCore"']

gs_plugin_generic_updates = shared_module(
  'gs_plugin_generic-updates',
  sources : 'gs-plugin-generic-updates.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
)

gs_plugin_provenance = shared_module(
  'gs_plugin_provenance',
  sources : 'gs-plugin-provenance.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
)

gs_plugin_provenance_license = shared_module(
  'gs_plugin_provenance-license',
  sources : 'gs-plugin-provenance-license.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
)

gs_plugin_icons = shared_module(
  'gs_plugin_icons',
  sources : 'gs-plugin-icons.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
)

gs_plugin_appstream = shared_module(
  'gs_plugin_appstream',
  sources : [
    'gs-plugin-appstream.c'
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_libs,
    libxmlb,
  ],
)

gs_plugin_hardcoded_blocklist = shared_module(
  'gs_plugin_hardcoded-blocklist',
  sources : 'gs-plugin-hardcoded-blocklist.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
)

gs_plugin_os_release = shared_module(
  'gs_plugin_os-release',
  sources : 'gs-plugin-os-release.c',
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
)

subdir('tests')
