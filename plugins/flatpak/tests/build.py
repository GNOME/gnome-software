#!/usr/bin/python3

import subprocess
import os
import shutil

def build_flatpak(appid, srcdir, repodir, cleanrepodir=True):
    print('Building %s from %s into %s' % (appid, srcdir, repodir))

    # delete repodir
    if cleanrepodir and os.path.exists(repodir):
        print("Deleting %s" % repodir)
        shutil.rmtree(repodir)

    # delete exportdir
    exportdir = os.path.join(srcdir, appid, 'export')
    if os.path.exists(exportdir):
        print("Deleting %s" % exportdir)
        shutil.rmtree(exportdir)

    # use git master where available
    local_checkout = '/home/hughsie/Code/flatpak'
    if os.path.exists(local_checkout):
        flatpak_cmd = os.path.join(local_checkout, 'flatpak')
    else:
        flatpak_cmd = 'flatpak'

    # runtimes have different defaults
    if appid.find('Runtime') != -1:
        is_runtime = True
        prefix = 'usr'
    else:
        is_runtime = False
        prefix = 'files'

    # finish the build
    argv = [flatpak_cmd, 'build-finish']
    argv.append(os.path.join(srcdir, appid))
    subprocess.call(argv)

    # compose AppStream data
    argv = ['appstream-compose']
    argv.append('--origin=flatpak')
    argv.append('--basename=%s' % appid)
    argv.append('--prefix=%s' % os.path.join(srcdir, appid, prefix))
    argv.append('--output-dir=%s' % os.path.join(srcdir, appid, prefix, 'share/app-info/xmls'))
    argv.append(appid)
    subprocess.call(argv)

    # export into repo
    argv = [flatpak_cmd, 'build-export']
    argv.append(repodir)
    argv.append(os.path.join(srcdir, appid))
    argv.append('--update-appstream')
    argv.append('--timestamp=2016-09-15T01:02:03')
    if is_runtime:
        argv.append('--runtime')
    subprocess.call(argv)

def copy_repo(srcdir, destdir):
    srcdir_repo = os.path.join(srcdir, 'repo')
    destdir_repo = os.path.join(destdir, 'repo')
    print("Copying %s to %s" % (srcdir_repo, destdir_repo))
    if os.path.exists(destdir_repo):
        shutil.rmtree(destdir_repo)
    shutil.copytree(srcdir_repo, destdir_repo)

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
copy_repo('app-with-runtime', 'app-update')
build_flatpak('org.test.Chiron',
              'app-update',
              'app-update/repo',
              cleanrepodir=False)

# just a runtime present
build_flatpak('org.test.Runtime',
              'only-runtime',
              'only-runtime/repo')
