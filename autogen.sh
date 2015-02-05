#!/bin/sh
# Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
#
# Run this to generate all the initial makefiles, etc.
#
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

(test -f $srcdir/configure.ac) || {
    echo -n "**Error**: Directory \"\'$srcdir\'\" does not look like the"
    echo " top-level package directory"
    exit 1
}

which gnome-autogen.sh || {
    echo "You need to install gnome-common!"
    exit 1
}

REQUIRED_AUTOMAKE_VERSION=1.7 GNOME_DATADIR="$gnome_datadir" . gnome-autogen.sh
