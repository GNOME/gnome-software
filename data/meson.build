subdir('appdata')
subdir('icons')

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

install_data([
  'upgrade-bg.png',
  'featured-ardour.png',
  'featured-ardour-bg.png',
  'featured-blender.png',
  'featured-builder.png',
  'featured-builder-bg.jpg',
  'featured-chess.png',
  'featured-firefox.png',
  'featured-gimp.png',
  'featured-gnome-sudoku.png',
  'featured-inkscape.svg',
  'featured-maps.png',
  'featured-maps-bg.png',
  'featured-mypaint.png',
  'featured-polari.svg',
  'featured-transmission.png',
  'featured-weather.png',
  'featured-weather-bg.png'
  ],
  install_dir : 'share/gnome-software'
)
