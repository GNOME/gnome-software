#!/bin/bash

set -e

unzip subprojects.meson.zip -d subprojects.meson
meson subprojects download --sourcedir subprojects.meson
rm subprojects.meson/subprojects/*.wrap
mv subprojects.meson/subprojects .
# allow updating this one without a docker rebuild
rm -rf subprojects.meson
