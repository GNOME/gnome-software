subdir('metainfo')
subdir('icons')
subdir('assets')

cdata = configuration_data()
cdata.set('bindir', join_paths(get_option('prefix'),
                               get_option('bindir')))

# note: the main .desktop file is in src/
# this one is for the background service
# replace @bindir@
configure_file(
  input : 'autostart/org.gnome.Software.desktop.in',
  output : 'org.gnome.Software.desktop',
  install_dir: join_paths(get_option('sysconfdir'), 'xdg/autostart'),
  configuration : cdata
)

compiled_schemas = gnome.compile_schemas(
  depend_files: 'org.gnome.software.gschema.xml')
install_data('org.gnome.software.gschema.xml',
             install_dir : join_paths(get_option('datadir'), 'glib-2.0', 'schemas'))

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
