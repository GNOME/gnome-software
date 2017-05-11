subdir('appdata')
subdir('icons')
subdir('assets')

compiled = gnome.compile_schemas()
install_data('org.gnome.software.gschema.xml',
             install_dir : 'share/glib-2.0/schemas')

if get_option('enable-external-appstream')
  # replace @libexecdir@
  configure_file(
    input : 'org.gnome.software.external-appstream.policy.in',
    output : 'org.gnome.software.external-appstream.policy',
    install: true,
    install_dir: join_paths(get_option('datadir'), 'polkit-1', 'actions'),
    configuration : conf
  )
endif
