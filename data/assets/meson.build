install_data([
  'upgrade-bg.png',
  'featured-boxes.svg',
  'featured-bitwarden.svg',
  'featured-calibre-bg.jpg',
  'featured-darktable-bg.jpg',
  'featured-dropbox.png',
  'featured-FeedReader.svg',
  'featured-foliate.svg',
  'featured-foliate-bg.jpg',
  'featured-fondo-bg.jpg',
  'featured-gimp.png',
  'featured-gnome-sudoku.png',
  'featured-homebank-bg.jpg',
  'featured-inkscape.svg',
  'featured-kdenlive-bg.jpg',
  'featured-klavaro-bg.jpg',
  'featured-maps-bg.svg',
  'featured-maps-overlay.svg',
  'featured-marker-bg.jpg',
  'featured-mypaint.svg',
  'featured-pdf-slicer.svg',
  'featured-podcasts.svg',
  'featured-polari.svg',
  'featured-slack-bg.jpg',
  'featured-sound-recorder.svg',
  'featured-stellarium-bg.jpg',
  'featured-tetravex-bg.jpg',
  'featured-todo-bg.jpg',
  'featured-transmission.svg',
  'featured-uberwriter.svg',
  'featured-vlc-bg.jpg'
  ],
  install_dir : 'share/gnome-software'
)

if get_option('default_featured_apps')
  install_data('org.gnome.Software.Featured.xml',
    install_dir : join_paths(get_option('datadir'), 'app-info', 'xmls'),
  )
endif
