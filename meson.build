project('gnome-software', 'c',
  version : '49.rc',
  license : 'GPL-2.0-or-later',
  default_options : ['warning_level=2', 'c_std=c11'],
  meson_version : '>=1.6.0'
)

conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('PACKAGE_NAME', meson.project_name())
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('BUILD_TYPE', get_option('buildtype'))

build_profile = get_option('profile')
if get_option('buildtype') == 'release'
  build_profile = ''
endif

conf.set_quoted('BUILD_PROFILE', build_profile)

application_id = 'org.gnome.Software' + build_profile
conf.set_quoted('APPLICATION_ID', application_id)

# this refers to the gnome-software plugin API version
# this is not in any way related to a package or soname version
gs_plugin_api_version = '23'
conf.set_quoted('GS_PLUGIN_API_VERSION', gs_plugin_api_version)

datadir = join_paths(get_option('prefix'), get_option('datadir'))
libexecdir = join_paths(get_option('prefix'), get_option('libexecdir'))

# private subdirectory of libdir for the private shared libgnomesoftware to live in
gs_private_libdir = join_paths(get_option('prefix'), get_option('libdir'), 'gnome-software')

# install docs
install_data('README.md', install_dir : 'share/doc/gnome-software')

# get supported warning flags
test_args = [
  '-fstack-protector-strong',
  '-Waggregate-return',
  '-Warray-bounds',
  '-Wcast-align',
  '-Wclobbered',
  '-Wdeclaration-after-statement',
  '-Wempty-body',
  '-Wenum-compare',
  '-Wenum-conversion',
  '-Wenum-int-mismatch',
  '-Wformat=2',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Wformat-signedness',
  '-Wignored-qualifiers',
  '-Wimplicit-function-declaration',
  '-Werror=implicit-function-declaration',
  '-Winit-self',
  '-Wmaybe-uninitialized',
  '-Wmissing-declarations',
  '-Wmissing-format-attribute',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wmissing-parameter-type',
  '-Wmissing-prototypes',
  '-Wnested-externs',
  '-Werror=nested-externs',
  '-Wno-missing-field-initializers',
  '-Wno-strict-aliasing',
  '-Wno-suggest-attribute=format',
  '-Wno-unused-parameter',
  '-Wnull-dereference',
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
  if cc.has_link_argument(arg)
    global_link_args += arg
  endif
endforeach
if not meson.is_subproject()
  add_global_link_arguments(
    global_link_args,
    language: 'c'
  )
endif

# Needed for PATH_MAX and symlink()
add_project_arguments('-D_XOPEN_SOURCE=700', language : 'c')
# Needed for syscall()
add_project_arguments('-D_GNU_SOURCE', language : 'c')

conf.set('HAVE_LINUX_UNISTD_H', cc.has_header('linux/unistd.h'))

appstream = dependency('appstream',
  version : '>= 0.16.4',
  fallback : ['appstream', 'appstream_dep'],
  default_options : [
    'docs=false',
    'apidocs=false',
    'install-docs=false'
  ]
)

gdk_pixbuf = dependency('gdk-pixbuf-2.0', version : '>= 2.32.0')
libxmlb = dependency('xmlb', version : '>= 0.3.4', fallback : ['libxmlb', 'libxmlb_dep'])
gio_unix = dependency('gio-unix-2.0', version : '>= 2.56.0')
gmodule = dependency('gmodule-2.0')
gtk = dependency('gtk4',
  version : '>= 4.16.0',
  fallback: ['gtk', 'gtk_dep'],
  default_options: [
      'build-demos=false',
      'build-examples=false',
      'build-tests=false',
      'build-testsuite=false',
      'introspection=disabled',
      'vulkan=disabled',
    ]
  )
glib = dependency('glib-2.0', version : '>= 2.76.0')
gsettings_desktop_schemas = dependency('gsettings-desktop-schemas', version : '>= 3.18.0')
json_glib = dependency('json-glib-1.0', version : '>= 1.6.0')
libm = cc.find_library('m', required: false)
libsoup = dependency('libsoup-3.0', version : '>= 3.0')
libadwaita = dependency('libadwaita-1',
  version: '>=1.6.0',
  fallback: ['libadwaita', 'libadwaita_dep'],
  default_options: [
    'examples=false',
    'introspection=disabled',
    'tests=false',
    'vapi=false',
  ]
)

# This should be available in GLib 2.74
if meson.get_compiler('c').has_header_symbol('glib.h', 'G_FORMAT_SIZE_ONLY_VALUE', dependencies: glib)
  conf.set('HAVE_G_FORMAT_SIZE_ONLY_VALUE', '1')
endif

libsysprof_capture_dep = dependency('sysprof-capture-4',
  required: get_option('sysprof'),
  default_options: [
    'enable_examples=false',
    'enable_gtk=false',
    'enable_tests=false',
    'enable_tools=false',
    'libsysprof=false',
    'with_sysprofd=none',
    'help=false',
  ],
  fallback: ['sysprof', 'libsysprof_capture_dep'],
)
conf.set('HAVE_SYSPROF', libsysprof_capture_dep.found())

if get_option('mogwai')
  mogwai_schedule_client = dependency('mogwai-schedule-client-0', version : '>= 0.2.0')
  conf.set('HAVE_MOGWAI', 1)
endif

if get_option('polkit')
  polkit = dependency('polkit-gobject-1')
  conf.set('HAVE_POLKIT', 1)
endif

if get_option('packagekit')
  packagekit = dependency('packagekit-glib2', version : '>= 1.2.5')
  conf.set('HAVE_PACKAGEKIT', '1')
  add_project_arguments('-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE',
                        language : 'c')
  if get_option('packagekit_autoremove')
      conf.set('GS_PACKAGEKIT_AUTOREMOVE', 'TRUE')
  else
      conf.set('GS_PACKAGEKIT_AUTOREMOVE', 'FALSE')
  endif
endif

if get_option('eos_updater')
  ostree = dependency('ostree-1')
endif

if get_option('fwupd')
  fwupd = dependency('fwupd', version : '>= 1.6.2')
endif

if get_option('flatpak')
  flatpak = dependency('flatpak', version : '>= 1.14.1')
  ostree = dependency('ostree-1')

  flatpak_httpbackend = flatpak.get_variable('httpbackend')
  if flatpak_httpbackend == 'soup'
    flatpak_soupapiversion = flatpak.get_variable('soupapiversion', default_value: '2')
    if flatpak_soupapiversion.version_compare('< 3')
      error('''Flatpak library has been built with 'libsoup2' http backend, which conflicts with 'libsoup3' used by gnome-software.

You can either:

1. Build gnome-software without flatpak support (with -Dflatpak=false) or
2. Rebuild flatpak library with http backend other than 'libsoup2'.''')
    else
      flatpak_summary = '@0@ (@1@)'.format(flatpak_httpbackend, flatpak_soupapiversion)
    endif
  else
    flatpak_summary = flatpak_httpbackend
  endif
  summary('Flatpak http backend', '@0@'.format(flatpak_summary), section : 'Misc')
endif

if get_option('malcontent')
  malcontent = dependency('malcontent-0', version: '>= 0.5.0', fallback: ['malcontent', 'libmalcontent_dep'], default_options: ['ui=disabled'])
endif

if get_option('rpm_ostree')
  ostree = dependency('ostree-1')
  rpm = dependency('rpm')
  rpm_ostree = dependency('rpm-ostree-1', version : '>= 2019.3')
endif

if get_option('gudev')
  gudev = dependency('gudev-1.0')
endif

if get_option('snap')
    snap = dependency('snapd-glib-2', version : '>= 1.64')
endif

if get_option('hardcoded_foss_webapps') or get_option('hardcoded_proprietary_webapps')
  assert(get_option('webapps'), 'webapps option must be true when hardcoded_foss_webapps/hardcoded_proprietary_webapps is')
  pwa_list_proj = subproject('gnome-pwa-list')
endif
if get_option('hardcoded_foss_webapps')
  pwa_list_foss = pwa_list_proj.get_variable('gnome_pwa_list_foss')
  install_data(pwa_list_foss,
    install_dir : join_paths(datadir, 'swcatalog', 'xml'),
  )
endif
if get_option('hardcoded_proprietary_webapps')
  pwa_list_proprietary = pwa_list_proj.get_variable('gnome_pwa_list_proprietary')
  install_data(pwa_list_proprietary,
    install_dir : join_paths(datadir, 'swcatalog', 'xml'),
  )
endif

systemd = dependency('systemd', required : false)
systemd_user_unit_dir = systemd.get_variable(
    'systemd_user_unit_dir',
    pkgconfig_define : ['prefix', get_option('prefix')],
    default_value: get_option('prefix') / 'lib' / 'systemd' / 'user'
)

gnome = import('gnome')
i18n = import('i18n')

conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf.set_quoted('LOCALEDIR',
                join_paths(get_option('prefix'),
                           get_option('localedir')))
conf.set_quoted('DATADIR', datadir)
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
conf.set_quoted('LIBEXECDIR', libexecdir)
conf.set('ENABLE_EXTERNAL_APPSTREAM', get_option('external_appstream'))
conf.set('ENABLE_DKMS', get_option('dkms'))
configure_file(
  output : 'config.h',
  configuration : conf
)

add_project_arguments('-DI_KNOW_THE_GNOME_SOFTWARE_API_IS_SUBJECT_TO_CHANGE',
                      language : 'c')

common_test_env = [
  'G_DEBUG=gc-friendly',
  'G_ENABLE_DIAGNOSTIC=1',
  'MALLOC_CHECK_=2',
  'GSETTINGS_SCHEMA_DIR=@0@/data/'.format(meson.project_build_root()),
  'GSETTINGS_BACKEND=memory',

  # Make flatpak skip parental controls since it requires the system bus.
  # https://github.com/flatpak/flatpak/issues/2993
  'FLATPAK_SYSTEM_HELPER_ON_SESSION=1',
]

installed_tests_metadir = join_paths(datadir, 'installed-tests', meson.project_name())
installed_tests_execdir = join_paths(libexecdir, 'installed-tests', meson.project_name())
installed_tests_enabled = get_option('installed_tests')
installed_tests_template = files('tests/template.test.in')

add_test_setup('default',
  is_default: not meson.is_subproject(),
  env: common_test_env,
  timeout_multiplier: 2,
)

subdir('data')
subdir('lib')
subdir('plugins')
subdir('src')
if get_option('external_appstream')
  subdir('gs-install-appstream')
endif
subdir('po')
subdir('doc')
subdir('help')

# FIXME: remove when https://github.com/mesonbuild/meson/issues/837 fixed
meson.add_install_script('meson_post_install.sh')
