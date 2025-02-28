#
# Copyright (c) 2024 Codethink Limited
# Copyright (c) 2024 GNOME Foundation
#
# SPDX-License-Identifier: GPL-2.0-or-later
#

cargs = ['-DG_LOG_DOMAIN="GsPluginSystemdSysupdate"']

systemd_sysupdated_generated = gnome.gdbus_codegen(
  'gs-systemd-sysupdated-generated',
  sources : ['org.freedesktop.sysupdate1.xml'],
  interface_prefix : 'org.freedesktop.sysupdate1',
  namespace : 'GsSystemdSysupdate',
  extra_args : [ '--glib-min-required=' + glib.version() ],
)

shared_module(
  'gs_plugin_systemd-sysupdate',
  systemd_sysupdated_generated,
  sources : [
    'gs-plugin-systemd-sysupdate.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : plugin_libs,
)

if get_option('tests')
  test_env = environment()
  test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
  test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

  cargs += ['-DLOCALPLUGINDIR="' + meson.current_build_dir() + '"']
  cargs += ['-DLOCALPLUGINDIR_CORE="' + meson.current_build_dir() + '/../core"']

  e = executable(
    'gs-self-test-systemd-sysupdate',
    systemd_sysupdated_generated,
    compiled_schemas,
    sources : [
      'gs-self-test.c'
    ],
    include_directories : [
      include_directories('../..'),
      include_directories('../../lib'),
    ],
    dependencies : [
      plugin_libs,
    ],
    c_args : cargs,
  )
  test('gs-self-test-systemd-sysupdate', e, suite: ['plugins', 'systemd-sysupdate'], env: test_env)

  subdir('services')
endif
