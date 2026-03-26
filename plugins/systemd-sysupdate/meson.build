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

gs_plugin_systemd_sysupdate = shared_module(
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

subdir('tests')
