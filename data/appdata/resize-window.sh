#!/bin/sh

# aim for a size of 1200x675, but wmctrl doesn't seem to take into account
# the size of the window borders
wmctrl -r Software -e 0,-1,-1,1224,700
wmctrl -a Software
