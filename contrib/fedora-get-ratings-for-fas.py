#!/usr/bin/env python
#
# Copyright (C) 2013 Ralph Bean <rbean@redhat.com>
# Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU Lesser General Public License Version 2.1
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA

import pprint
import sqlite3
import yum

#wget https://admin.fedoraproject.org/pkgdb/lists/sqlitebuildtags/F-16-x86_64-u
#wget https://admin.fedoraproject.org/pkgdb/lists/sqlitebuildtags/F-devel-x86_64
conn = sqlite3.connect('F-devel-x86_64')
 
c = conn.cursor()
 
c.execute('SELECT * from packagetags;')
rows = c.fetchall()
conn.close()
 
# Build up a dict that just sums the score of all tags for a package.
result = {}
for package, tag, score in rows:
    result[package] = result.get(package, 0) + score
 
# normalize that to between 0..1
top = float(max(result.values()))
result = dict([(name, value / top) for name, value in result.items()])

# setup yum
yb = yum.YumBase()
yb.doConfigSetup(errorlevel=-1, debuglevel=-1)
yb.conf.cache = 0

# get all the packages yum knows about
pkgs = yb.pkgSack

rating_results = []
average = 0
num_in_average = 0

for package, tag in result.items():
    if tag < 0.01:
        continue
    for pkg in pkgs:
        if pkg.name != package:
            continue

        # find out if any of the files ship a desktop file
        desktop_ids = []
        for instfile in pkg.returnFileEntries():
            if instfile.startswith('/usr/share/applications/') and instfile.endswith('.desktop'):
                desktop_id = instfile[24:]
                if desktop_id.startswith("fedora-"):
                    desktop_id = desktop_id[7:]
                desktop_id = desktop_id.replace(".desktop","")
                if desktop_id.find("system-config") != -1:
                    continue
                if desktop_id.find("screensaver") != -1:
                    continue
                # will have to modify this if KDE/XFCE ever consume this data
                if desktop_id.startswith("kde"):
                    continue
                if desktop_id.find("kde4") != -1:
                    continue
                if desktop_id.startswith("mate-"):
                    continue
                if desktop_id.find("qt4") != -1:
                    continue
                if desktop_id.startswith("qt"):
                    continue
                if desktop_id.startswith("xfce4-"):
                    continue
                if desktop_id.startswith("lxde-"):
                    continue
                if desktop_id.startswith("security-"):
                    continue
                if desktop_id.startswith("puzzle-"):
                    continue
                if desktop_id.startswith("wine-"):
                    continue
                if desktop_id.find("-settings") != -1:
                    continue
                if desktop_id.find("-panel") != -1:
                    continue
                # extract the desktop-id from the filename
                # NOTE: prepending a vendor prefix does *NOT* make this any easier
                print "found", desktop_id
                desktop_ids.append(desktop_id)

        # don't download packages without desktop files
        if len(desktop_ids) == 0:
            continue
        print "mapped", package, "to", desktop_ids
        for desktop_id in desktop_ids:
            rating_results.append((desktop_id, tag))
        average = average + tag;
        num_in_average = num_in_average + 1
        break

average = average / num_in_average

# weight to average at 50
scale_factor = 50 / average;

# sort
rating_results = sorted(rating_results, key=lambda rating: rating[0])

for desktop_id, tag in rating_results:
    rating = int(tag * scale_factor)
    if rating > 100:
        rating = 100
    if rating > 0:
        #print "%i\t%s" % (rating, desktop_id)
        print "\t\t{ %i,\t\"%s\" }," % (rating, desktop_id)

