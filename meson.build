project('gnome-software', 'c',
  version : '3.30.1',
  license : 'GPL-2.0+',
  default_options : ['warning_level=1', 'c_std=c99'],
  meson_version : '>=0.40.0'
)

conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('PACKAGE_NAME', meson.project_name())
conf.set_quoted('PACKAGE_VERSION', meson.project_version())

# this refers to the gnome-software plugin API version
# this is not in any way related to a package or soname version
gs_plugin_api_version = '12'
conf.set_quoted('GS_PLUGIN_API_VERSION', gs_plugin_api_version)

# install docs
install_data('README.md', install_dir : 'share/doc/gnome-software')

# get suported warning flags
test_args = [
  '-fstack-protector-strong',
  '-Waggregate-return',
  '-Warray-bounds',
  '-Wcast-align',
  '-Wclobbered',
  '-Wdeclaration-after-statement',
  '-Wempty-body',
  '-Wextra',
  '-Wformat=2',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Wformat-signedness',
  '-Wignored-qualifiers',
  '-Wimplicit-function-declaration',
  '-Winit-self',
  '-Winline',
  '-Wmissing-declarations',
  '-Wmissing-format-attribute',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wmissing-parameter-type',
  '-Wmissing-prototypes',
  '-Wnested-externs',
  '-Wno-discarded-qualifiers',
  '-Wno-missing-field-initializers',
  '-Wno-strict-aliasing',
  '-Wno-suggest-attribute=format',
  '-Wno-unused-parameter',
  '-Wold-style-definition',
  '-Woverride-init',
  '-Wpacked',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wreturn-type',
  '-Wshadow',
  '-Wsign-compare',
  '-Wstrict-aliasing',
  '-Wstrict-prototypes',
  '-Wswitch-default',
  '-Wtype-limits',
  '-Wundef',
  '-Wuninitialized',
  '-Wunused-but-set-variable',
  '-Wwrite-strings'
]
cc = meson.get_compiler('c')
foreach arg: test_args
  if cc.has_argument(arg)
    add_project_arguments(arg, language : 'c')
  endif
endforeach

# enable full RELRO where possible
# FIXME: until https://github.com/mesonbuild/meson/issues/1140 is fixed
global_link_args = []
test_link_args = [
  '-Wl,-z,relro',
  '-Wl,-z,now',
]
foreach arg: test_link_args
  if cc.has_argument(arg)
    global_link_args += arg
  endif
endforeach
add_global_link_arguments(
  global_link_args,
  language: 'c'
)

# Needed for PATH_MAX and symlink()
add_project_arguments('-D_XOPEN_SOURCE=700', language : 'c')
# Needed for syscall()
add_project_arguments('-D_GNU_SOURCE', language : 'c')

conf.set('HAVE_LINUX_UNISTD_H', cc.has_header('linux/unistd.h'))

appstream_glib = dependency('appstream-glib', version : '>= 0.7.3')
gdk_pixbuf = dependency('gdk-pixbuf-2.0', version : '>= 2.32.0')
gio_unix = dependency('gio-unix-2.0')
gmodule = dependency('gmodule-2.0')
gtk = dependency('gtk+-3.0', version : '>= 3.22.4')
json_glib = dependency('json-glib-1.0', version : '>= 1.2.0')
libm = cc.find_library('m', required: false)
libsecret = dependency('libsecret-1')
libsoup = dependency('libsoup-2.4', version : '>= 2.52.0')
sqlite3 = dependency('sqlite3')

if get_option('valgrind')
  message(meson.version())
  # urgh, meson is broken
  if meson.version().version_compare('>0.41.0')
    valgrind = dependency('valgrind')
    conf.set('USE_VALGRIND', 1)
  else
    message('Valgrind integration disabled to work around a meson bug')
    valgrind = []
  endif
else
  message('Valgrind integration disabled')
  valgrind = []
endif

if get_option('gnome_desktop')
  gnome_desktop = dependency('gnome-desktop-3.0', version : '>= 3.18.0')
  conf.set('HAVE_GNOME_DESKTOP', 1)
endif

if get_option('gspell')
  gspell = dependency('gspell-1')
  conf.set('HAVE_GSPELL', 1)
endif

if get_option('polkit')
  polkit = dependency('polkit-gobject-1')
  conf.set('HAVE_POLKIT', 1)
endif

if get_option('packagekit')
  packagekit = dependency('packagekit-glib2', version : '>= 1.1.0')
  conf.set('HAVE_PACKAGEKIT', '1')
  add_project_arguments('-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE',
                        language : 'c')
  if get_option('packagekit_autoremove')
      conf.set('GS_PACKAGEKIT_AUTOREMOVE', 'TRUE')
  else
      conf.set('GS_PACKAGEKIT_AUTOREMOVE', 'FALSE')
  endif
endif

if get_option('fwupd')
  fwupd = dependency('fwupd', version : '>= 1.0.3')
endif

if get_option('flatpak')
  flatpak = dependency('flatpak', version : '>= 0.99.3')
endif

if get_option('rpm_ostree')
  ostree = dependency('ostree-1')
  rpm_ostree = dependency('rpm-ostree-1', version : '>= 2018.4')
endif

if get_option('ubuntu_reviews')
  oauth = dependency('oauth')
endif

if get_option('gudev')
  gudev = dependency('gudev-1.0')
endif

if get_option('snap')
  snap = dependency('snapd-glib', version : '>= 1.41')
endif

gnome = import('gnome')
i18n = import('i18n')

conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf.set_quoted('LOCALEDIR',
                join_paths(get_option('prefix'),
                           get_option('localedir')))
conf.set_quoted('DATADIR',
                join_paths(get_option('prefix'),
                           get_option('datadir')))
conf.set_quoted('LIBDIR',
                join_paths(get_option('prefix'),
                           get_option('libdir')))
conf.set_quoted('BINDIR',
                join_paths(get_option('prefix'),
                           get_option('bindir')))
conf.set_quoted('SYSCONFDIR',
                join_paths(get_option('prefix'),
                           get_option('sysconfdir')))
conf.set_quoted('LOCALSTATEDIR',
                join_paths(get_option('prefix'),
                           get_option('localstatedir')))
conf.set_quoted('LIBEXECDIR',
                join_paths(get_option('prefix'),
                           get_option('libexecdir')))
configure_file(
  output : 'config.h',
  configuration : conf
)

add_project_arguments('-DI_KNOW_THE_GNOME_SOFTWARE_API_IS_SUBJECT_TO_CHANGE',
                      language : 'c')

test_env = [
  'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
  'GSETTINGS_SCHEMA_DIR=@0@/data/'.format(meson.build_root()),
  'GSETTINGS_BACKEND=memory',
  'MALLOC_CHECK_=2',
]

subdir('data')
subdir('lib')
subdir('plugins')
subdir('src')
subdir('po')
subdir('doc')

if meson.version().version_compare('<0.41.0')
  archiver = find_program('git', required : false)
  if archiver.found()
    run_target('dist',
      # git config tar.tar.xz.command "xz -c"
      command: [
        'git', 'archive',
        '--prefix=' + meson.project_name() + '-' + meson.project_version() + '/',
        'HEAD',
        '--format=tar.xz',
        '--output',
        meson.project_name() + '-' + meson.project_version() + '.tar.xz'
      ]
    )
  else
    message('git not found, you will not be able to run `ninja dist`')
  endif
endif

# FIXME: remove when https://github.com/mesonbuild/meson/issues/837 fixed
meson.add_install_script('meson_post_install.sh')
