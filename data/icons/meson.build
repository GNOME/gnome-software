# These have to be installed system-wide so that gnome-shell can read when when showing notifications
component_icons = [
  'system-component-addon.svg',
  'system-component-application.svg',
  'system-component-codecs.svg',
  'system-component-driver.svg',
  'system-component-firmware.svg',
  'system-component-input-sources.svg',
  'system-component-language.svg',
  'system-component-os-updates.svg',
  'system-component-runtime.svg',
]

foreach icon: component_icons
  install_data(icon,
               install_dir : 'share/icons/hicolor/scalable/categories')
endforeach

subdir('hicolor')
