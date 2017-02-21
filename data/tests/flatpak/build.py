#!/bin/python

import subprocess
import os
import shutil

def build_flatpak(appid, srcdir, repodir, cleanrepodir=True):
    print 'Building %s from %s into %s' % (appid, srcdir, repodir)

    # delete repodir
    if cleanrepodir and os.path.exists(repodir):
        print "Deleting %s" % repodir
        shutil.rmtree(repodir)

    # delete exportdir
    exportdir = os.path.join(srcdir, appid, 'export')
    if os.path.exists(exportdir):
        print "Deleting %s" % exportdir
        shutil.rmtree(exportdir)

    # use git master where available
    local_checkout = '/home/hughsie/Code/flatpak'
    if os.path.exists(local_checkout):
        flatpak_cmd = os.path.join(local_checkout, 'flatpak')
    else:
        flatpak_cmd = 'flatpak'

    # finish the build
    argv = [flatpak_cmd, 'build-finish']
    argv.append(os.path.join(srcdir, appid))
    subprocess.call(argv)

    # compose AppStream data
    argv = ['appstream-compose']
    argv.append('--origin=flatpak')
    argv.append('--basename=%s' % appid)
    argv.append('--prefix=%s' % os.path.join(srcdir, appid, 'files'))
    argv.append('--output-dir=%s' % os.path.join(srcdir, appid, 'files/share/app-info/xmls'))
    argv.append(appid)
    subprocess.call(argv)

    # export into repo
    argv = [flatpak_cmd, 'build-export']
    argv.append(repodir)
    argv.append(os.path.join(srcdir, appid))
    argv.append('--update-appstream')
    if appid.find('Runtime') != -1:
        argv.append('--runtime')
    subprocess.call(argv)

# normal app with runtime in same remote
build_flatpak('org.test.Chiron',
              'app-with-runtime',
              'app-with-runtime/repo')
build_flatpak('org.test.Runtime',
              'app-with-runtime',
              'app-with-runtime/repo',
              cleanrepodir=False)

# app referencing remote that cannot be found
build_flatpak('org.test.Chiron',
              'app-with-runtime',
              'app-missing-runtime/repo')

# app with an update
build_flatpak('org.test.Chiron',
              'app-with-runtime',
              'app-update/repo')
build_flatpak('org.test.Runtime',
              'app-with-runtime',
              'app-update/repo',
              cleanrepodir=False)
build_flatpak('org.test.Chiron',
              'app-update',
              'app-update/repo',
              cleanrepodir=False)
