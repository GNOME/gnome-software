cargs = ['-DG_LOG_DOMAIN="GsPlugin"']

shared_module(
  'gs_plugin_generic-updates',
  sources : 'gs-plugin-generic-updates.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_key-colors',
  sources : 'gs-plugin-key-colors.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_key-colors-metadata',
  sources : 'gs-plugin-key-colors-metadata.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_provenance',
  sources : 'gs-plugin-provenance.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_provenance-license',
  sources : 'gs-plugin-provenance-license.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
  link_with : [
    libgnomesoftware
  ]
)


shared_module(
  'gs_plugin_icons',
  sources : 'gs-plugin-icons.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_appstream',
  sources : [
    'gs-appstream.c',
    'gs-plugin-appstream.c'
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_libs,
    libxmlb,
  ],
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_desktop-categories',
  sources : [
    'gs-plugin-desktop-categories.c',
    'gs-desktop-common.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_desktop-menu-path',
  sources : [
    'gs-plugin-desktop-menu-path.c',
    'gs-desktop-common.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_hardcoded-blacklist',
  sources : 'gs-plugin-hardcoded-blacklist.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
  link_with : [
    libgnomesoftware
  ]
)

if get_option('hardcoded_popular')
  shared_module(
    'gs_plugin_hardcoded-popular',
    sources : 'gs-plugin-hardcoded-popular.c',
    include_directories : [
      include_directories('../..'),
      include_directories('../../lib'),
    ],
    install : true,
    install_dir: plugin_dir,
    c_args : cargs,
    dependencies : plugin_libs,
    link_with : [
      libgnomesoftware
    ]
  )
endif

shared_module(
  'gs_plugin_rewrite-resource',
  sources : 'gs-plugin-rewrite-resource.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
  link_with : [
    libgnomesoftware
  ]
)

shared_module(
  'gs_plugin_os-release',
  sources : 'gs-plugin-os-release.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
  link_with : [
    libgnomesoftware
  ]
)

if get_option('tests')
  cargs += ['-DLOCALPLUGINDIR="' + meson.current_build_dir() + '"']
  cargs += ['-DTESTDATADIR="' + join_paths(meson.current_source_dir(), 'tests') + '"']
  e = executable(
    'gs-self-test-core',
    compiled_schemas,
    sources : [
      'gs-self-test.c',
      'gs-appstream.c'
    ],
    include_directories : [
      include_directories('../..'),
      include_directories('../../lib'),
    ],
    dependencies : [
      plugin_libs,
      libxmlb,
    ],
    link_with : [
      libgnomesoftware
    ],
    c_args : cargs,
  )
  test('gs-self-test-core', e, suite: ['plugins', 'core'], env: test_env)
endif
