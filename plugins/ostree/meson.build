cargs = ['-DG_LOG_DOMAIN="GsPluginOstree"']

rpmostree_generated = gnome.gdbus_codegen(
  'gs-rpmostree-generated',
  'org.projectatomic.rpmostree1.xml',
  interface_prefix : 'org.projectatomic.rpmostree1',
  namespace : 'GsRPMOSTree'
)

shared_module(
  'gs_plugin_rpm_ostree',
  rpmostree_generated,
  sources : 'gs-plugin-rpm-ostree.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs ]
)

shared_module(
  'gs_plugin_ostree',
  sources : 'gs-plugin-ostree.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, ostree ]
)

if get_option('enable-rpm')
  shared_module(
    'gs_plugin_rpm',
    sources : 'gs-plugin-rpm.c',
    include_directories : [
      include_directories('../..'),
      include_directories('../../lib'),
    ],
    install : true,
    install_dir: plugin_dir,
    c_args : cargs,
    dependencies : [ plugin_libs, rpm ]
  )
endif
