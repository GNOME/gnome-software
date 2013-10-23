# Introduction #

Plugins are modules that are loaded at runtime to provide information about
requests and to service user actions like installing, removing and updating.
Plugins are disabled by default, and need to be enabled manually before they
are used.
This allows different distributions to pick and choose how the application
installer gathers data just by setting a single GSettings key. For instance:

>  SuSE has AppStream data and also uses
> PackageKit. The default on SuSE would
> be  to enable the `appstream` and
> `packagekit` plugins and leave the
> rest disabled.

>  Fedora doesn't have AppStream data,
> so it has to make do with other local
> data.  On Fedora we would enable the
> `datadir-apps`, `desktop-db` and all
> the  `hardcoded` plugins to get some
> sensible results. For Fedora, it
> probably also  makes sense to write a
> plugin that interfaces with FAS or
> fedora-tagger to get  the extra
> metadata for uninstalled applications.

Plugins also have a priority system where the largest number gets run first.
That means if one plugin requires some property or metadata set by another
plugin then it **must** have a priority value that is greater to ensure the
property is available.

As we are letting the distribution pick and choose what plugins are run, we need
a way to 'refine' the results. For instance, the `packagekit` plugin returns
applications with a kind `PACKAGE` which will not be returned to the
UI for display as they are not applications.
The 'packagekit' plugin relies on other plugins like `appstream` or `desktopdb`
to add the required name, summary and icon data and to convert the `GsApp` to a `NORMAL`.
Furthermore, the 'hardcoded-kind' plugin may override the NORMAL
kind to a `SYSTEM` which means the application is core cannot be
removed by the user. Core apps would be things like nautilus and totem.

As a general rule, try to make plugins as small and self-contained as possible
and remember to cache as much data as possible for speed. Memory is cheap, time
less so.

---------------------------------------

## Plugins ##

In this document, application properties are specified as `[id]` meaning the
property called `id` and metadata (basically, random properites that we don't
want to export) are specified as `{sekret-property}`.
Adding and using metadata is quick as it's stored internally in a hash table,
so don't be afraid to add extra properties where it might make sense for other
plugins.

### dummy ###
Provides some dummy data that is useful in self test programs.

Overview:    | <p>
-------------|---
Methods:     | Search, AddUpdates, AddInstalled, AddPopular
Requires:    | `nothing`
Refines:     | `[id]->[name]`, `[id]->[summary]`

### hardcoded-kind ###
Provides some hardcoded static core applications that cannot be removed.

Overview:    | <p>
-------------|---
Methods:     | `nothing`
Requires:    | `nothing`
Refines:     | `[id]->[kind]`
Note:        | This is based on the gnome jhbuild moduleset

### hardcoded-popular ###
Provides some hardcoded static favourite applications.

Overview:    | <p>
-------------|---
Methods:     | AddPopular
Requires:    | `nothing`
Refines:     | `nothing`

### hardcoded-ratings ###
Provides some hardcoded static ratings for applications.

Overview:    | <p>
-------------|---
Methods:     | `nothing`
Requires:    | `nothing`
Refines:     | `[id]->[rating]`

### local-ratings ###
Provides a local SQL database of user-set ratings, useful for testing.

Overview:    | <p>
-------------|---
Methods:     | AppSetRating
Requires:    | `nothing`
Refines:     | `[id]->[rating]`

### packagekit ###
Uses the system PackageKit instance to return package data.

Overview:    | <p>
-------------|---
Methods:     | Search, AddUpdates, AddInstalled, AppInstall, AppRemove, AppUpdate
Requires:    | `[source-id]`
Refines:     | `[source-id]`, `[source]`, `{package-summary}`, `[update-details]`, `[management-plugin]`

### packagekit-refine ###
Uses the system PackageKit instance to return convert filenames to package-ids.

Overview:    | <p>
-------------|---
Methods:     | `nothing`
Requires:    | `{DataDir::desktop-filename}`
Refines:     | `[source-id]`, `[installed]`

### desktopdb ###
Uses the PackageKit desktop.db cache to map package names to, desktop names.

Overview:    | <p>
-------------|---
Methods:     | `nothing`
Requires:    | `nothing`
Refines:     | `[source]->{DataDir::desktop-filename}`

### appstream ###
Uses offline AppStream data to refine package results.

Overview:    | <p>
-------------|---
Methods:     | `AddCategoryApps`
Requires:    | `nothing`
Refines:     | `[source]->[name,summary,pixbuf,id,kind]`

### datadir-apps ###
Uses the files in /usr/share/applications to provide icons and, translations for
installed applications.

Overview:    | <p>
-------------|---
Methods:     | `nothing`
Requires:    | `nothing`
Refines:     | `{DataDir::desktop-filename}->[name]`, `{DataDir::desktop-filename}->[summary]`, `{DataDir::desktop-filename}->[pixbuf]`, `{DataDir::desktop-filename}->[id]`, `{DataDir::desktop-filename}->[kind]`

### datadir-filename ###
Uses the existance of a files in /usr/share/applications, named $id.desktop to
set the correct desktop filename., This is useful if we just have a bare `[id]`
from something like, AddPopular and want to get the details using 'datadir-apps'

Overview:    | <p>
-------------|---
Methods:     | `nothing`
Requires:    | `nothing`
Refines:     | `[id]->{DataDir::desktop-filename}`

### datadir-filename-local ###
Sets any applications not installed with datadir or /usr as installed as these
might have been installed manually or using jhbuild.

Overview:    | <p>
-------------|---
Methods:     | `nothing`
Requires:    | `nothing`
Refines:     | `{DataDir::desktop-filename}->[state]`, `[management-plugin]`
