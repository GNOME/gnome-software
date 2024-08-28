# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2024 Red Hat

shared_module(
  'gs_plugin_dkms',
  sources : 'gs-plugin-dkms.c',
  install : true,
  install_dir: plugin_dir,
  c_args : ['-DG_LOG_DOMAIN="GsPluginDkms"'],
  dependencies : plugin_libs,
)

executable(
  'gnome-software-dkms-helper',
  sources : [
    'gs-dkms-helper.c',
    '../../lib/gs-dkms-private.c',
  ],
  dependencies : [
    glib,
    gio_unix,
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  c_args : ['-DG_LOG_DOMAIN="GsDkmsHelper"'],
  install : true,
  install_dir : get_option('libexecdir'),
)
