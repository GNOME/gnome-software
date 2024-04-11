#
# Copyright (c) 2024 Codethink Limited
# Copyright (c) 2024 GNOME Foundation
#
# SPDX-License-Identifier: GPL-2.0-or-later
#

service_file = 'org.freedesktop.sysupdate1.service'

cdata = configuration_data()
cdata.set('abs_top_builddir', meson.project_build_root())

configure_file(
  input: service_file + '.in',
  output: service_file,
  configuration: cdata,
)
