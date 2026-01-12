cargs = ['-DG_LOG_DOMAIN="GsPluginPackageKit"']
deps = [
  plugin_libs,
  packagekit,
  gsettings_desktop_schemas,
]

if get_option('mogwai')
  deps += [mogwai_schedule_client]
endif

shared_module(
  'gs_plugin_packagekit',
  sources : [
    'gs-plugin-packagekit.c',
    'gs-packagekit-helper.c',
    'gs-packagekit-task.c',
    'packagekit-common.c',
    'gs-markdown.c',
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : deps,
)

subdir('tests')