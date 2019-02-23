cargs = ['-DG_LOG_DOMAIN="GsPluginRpmOstree"']

rpmostree_generated = gnome.gdbus_codegen(
  'gs-rpmostree-generated',
  'org.projectatomic.rpmostree1.xml',
  interface_prefix : 'org.projectatomic.rpmostree1',
  namespace : 'GsRPMOSTree'
)

shared_module(
  'gs_plugin_rpm-ostree',
  rpmostree_generated,
  sources : 'gs-plugin-rpm-ostree.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  install_rpath: join_paths(rpm_ostree.get_pkgconfig_variable('libdir'), 'rpm-ostree'),
  c_args : cargs,
  dependencies : [ plugin_libs, libdnf, ostree, rpm, rpm_ostree ],
  link_with : [
    libgnomesoftware
  ]
)
