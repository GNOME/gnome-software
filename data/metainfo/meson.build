i18n.merge_file(
  input: 'org.gnome.Software.metainfo.xml.in',
  output: 'org.gnome.Software.metainfo.xml',
  type: 'xml',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'metainfo')
)

if get_option('tests') and appstream.version().version_compare('>=0.15.5')
    ascli_exe = find_program('appstreamcli')
    test('gs-validate-metainfo',
        ascli_exe,
        args: ['validate',
              '--pedantic',
              '--no-net',
              '--explain',
              'org.gnome.Software.metainfo.xml.in'],
        workdir : meson.current_source_dir(),
        suite: ['lib'],
    )
endif
