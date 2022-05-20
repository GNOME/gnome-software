#!/bin/bash

set -e

git clone --depth 1 --no-tags https://gitlab.gnome.org/GNOME/gnome-software.git
meson subprojects download --sourcedir gnome-software
rm gnome-software/subprojects/*.wrap
mv gnome-software/subprojects/ .
# allow updating this one without a docker rebuild
rm -rf subprojects/gnome-pwa-list
rm -rf gnome-software
