Tools in GNOME Software for application developers
==================================================

GNOME Software is often where users will first come into contact with an
application which they might later install, so the impression the user gets of
that application is important. Application developers want to see how their
application will appear in GNOME Software, and to have some control over it.

GNOME Software provides some tools to help with this.

If there is a supported tool which is not in this document, please
[submit a merge request](https://gitlab.gnome.org/GNOME/gnome-software/-/merge_requests/new)
to document it.

Metainfo/Appstream
------------------

The information about an application comes from its metainfo (or, as it was
previously known: appdata) file. Metainfo files for multiple applications are
concatenated into ‘appstream’ files. Almost all of the customisation which GNOME
Software provides for applications comes from information in metainfo or
appstream files.

So the first thing to check for your application is that its metainfo file is
complete and valid. See the
[metainfo file specification](https://www.freedesktop.org/software/appstream/docs/).

To validate your metainfo file, run
```
appstreamcli validate /path/to/app.metainfo.xml
```

You can add this as a unit test to your application by adding the following to
the appropriate `meson.build` in your application:
```
metainfo_file = files('com.example.MyApp.metainfo.xml')
appstreamcli = find_program('appstreamcli', required: false)
if appstreamcli.found()
  test (
    'Validate metainfo file',
    appstreamcli,
    args: ['validate', '--no-net', metainfo_file],
  )
endif
```

Context tiles
-------------

The context tiles which are shown on an application’s details page in GNOME
Software are derived from the application’s metainfo.

There’s more detailed information about them, and the information they are built
from, [in the GNOME Software help](https://gnome.pages.gitlab.gnome.org/gnome-software/help/C/software-metadata.html).

Previewing the details page for an application
----------------------------------------------

GNOME Software allows previewing how an application will appear, by loading its
metainfo file directly. This allows previewing in-progress changes to an
application without publishing it to a repository.

Do this with the `--show-metainfo` argument:
```
gnome-software --show-metainfo=/path/to/app.metainfo.xml,icon=/path/to/icon.png
```

This will show the application in the details page of GNOME Software, and will
also display it in the featured carousel on the overview page.
