subdir('metainfo')
subdir('icons')
subdir('assets')

compiled_schemas = gnome.compile_schemas(
  depend_files: 'org.gnome.software.gschema.xml')
install_data('org.gnome.software.gschema.xml',
             install_dir : join_paths(get_option('datadir'), 'glib-2.0', 'schemas'))

if get_option('dkms')
  # replace @libexecdir@
  conf_data = configuration_data()
  conf_data.set('libexecdir', join_paths(get_option('prefix'),
                                         get_option('libexecdir')))
  out = configure_file(
    input : 'org.gnome.software.dkms-helper.policy.in.in',
    output : 'org.gnome.software.dkms-helper.policy.in',
    install: false,
    configuration : conf_data,
  )
  i18n.merge_file(
    input : out,
    output : 'org.gnome.software.dkms-helper.policy',
    po_dir: join_paths(meson.project_source_root(), 'po'),
    install: true,
    install_dir: join_paths(get_option('datadir'), 'polkit-1', 'actions'),
  )
endif

if get_option('external_appstream')
  # replace @libexecdir@
  conf_data = configuration_data()
  conf_data.set('libexecdir', join_paths(get_option('prefix'),
                                         get_option('libexecdir')))
  out = configure_file(
    input : 'org.gnome.software.external-appstream.policy.in.in',
    output : 'org.gnome.software.external-appstream.policy.in',
    install: false,
    configuration : conf_data,
  )
  i18n.merge_file(
    input : out,
    output : 'org.gnome.software.external-appstream.policy',
    po_dir: join_paths(meson.project_source_root(), 'po'),
    install: true,
    install_dir: join_paths(get_option('datadir'), 'polkit-1', 'actions'),
  )
endif

install_data('bash-completion/gnome-software',
             install_dir : join_paths(get_option('datadir'), 'bash-completion', 'completions'))
