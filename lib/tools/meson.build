# Test program to profile performance of the key-colors functions
executable(
  'profile-key-colors',
  sources : [
    'profile-key-colors.c',
    '../gs-key-colors.c',
    '../gs-key-colors.h',
  ],
  include_directories : [
    include_directories('..'),
    include_directories('../..'),
  ],
  dependencies : [
    glib,
    dependency('gdk-3.0'),
    gdk_pixbuf,
    libm,
  ],
  c_args : [
    '-Wall',
    '-Wextra',
  ],
  install: false,
)
