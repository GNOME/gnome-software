cargs = ['-DG_LOG_DOMAIN="GsPluginPackageKit"']
cargs += ['-DLOCALPLUGINDIR="' + meson.current_build_dir() + '"']

shared_module(
  'gs_plugin_systemd-updates',
  sources : [
    'gs-plugin-systemd-updates.c',
    'gs-packagekit-helper.c',
    'packagekit-common.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, packagekit ],
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_packagekit',
  sources : [
    'gs-plugin-packagekit.c',
    'gs-packagekit-helper.c',
    'packagekit-common.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, packagekit ],
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_packagekit-refine',
  sources : [
    'gs-plugin-packagekit-refine.c',
    'gs-markdown.c',
    'gs-packagekit-helper.c',
    'packagekit-common.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, packagekit ],
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_packagekit-refine-repos',
  sources : [
    'gs-plugin-packagekit-refine-repos.c',
    'gs-packagekit-helper.c',
    'packagekit-common.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, packagekit ],
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_packagekit-refresh',
  sources : [
    'gs-plugin-packagekit-refresh.c',
    'gs-packagekit-helper.c',
    'packagekit-common.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, packagekit ],
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_packagekit-local',
  sources : [
    'gs-plugin-packagekit-local.c',
    'gs-packagekit-helper.c',
    'packagekit-common.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, packagekit ],
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_packagekit-history',
  sources : [
    'gs-plugin-packagekit-history.c',
    'packagekit-common.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, packagekit ],
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_packagekit-upgrade',
  sources : [
    'gs-plugin-packagekit-upgrade.c',
    'gs-packagekit-helper.c',
    'packagekit-common.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, packagekit ],
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_packagekit-offline',
sources : [
    'gs-plugin-packagekit-offline.c',
    'packagekit-common.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, packagekit ],
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_packagekit-proxy',
sources : 'gs-plugin-packagekit-proxy.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, packagekit ],
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_packagekit-url-to-app',
  sources : [
    'gs-plugin-packagekit-url-to-app.c',
    'gs-packagekit-helper.c',
    'packagekit-common.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [ plugin_libs, packagekit ],
  link_with : [
    libgnomesoftware
  ]
)

if get_option('tests')
  cargs += ['-DTESTDATADIR="' + join_paths(meson.current_source_dir(), 'tests') + '"']
  e = executable(
    'gs-self-test-packagekit',
    compiled_schemas,
    sources : [
      'gs-markdown.c',
      'gs-self-test.c'
    ],
    include_directories : [
      include_directories('../..'),
      include_directories('../../lib'),
    ],
    dependencies : [
      plugin_libs,
    ],
    link_with : [
      libgnomesoftware
    ],
    c_args : cargs,
  )
  test('gs-self-test-packagekit', e, env: test_env)
endif
