i18n.merge_file(
  input: 'org.gnome.Software.metainfo.xml.in',
  output: 'org.gnome.Software.metainfo.xml',
  type: 'xml',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'metainfo')
)

if get_option('tests')
    ascli_exe = find_program('appstreamcli')
    test('gs-validate-metainfo',
        ascli_exe,
        args: ['validate',
              '--pedantic',
              '--no-net',
              'org.gnome.Software.metainfo.xml.in'],
        workdir : meson.current_source_dir(),
        suite: ['lib'],
    )
endif
