#!/bin/bash

sudo gdb --quiet --batch --nh --ex r --ex "t a a bt"  --ex quit --args /builds/GNOME/gnome-software/_build/plugins/flatpak/gs-self-test-flatpak
