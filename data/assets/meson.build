install_data([
  'upgrade-bg.png',
  ],
  install_dir : 'share/gnome-software'
)

if get_option('default_featured_apps')
  install_data('org.gnome.Software.Featured.xml',
    install_dir : join_paths(get_option('datadir'), 'app-info', 'xmls'),
  )
endif
