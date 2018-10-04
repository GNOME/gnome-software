[![Build Status](https://gitlab.gnome.org/GNOME/gnome-software/badges/master/build.svg)](https://gitlab.gnome.org/GNOME/gnome-software/pipelines)

# Software

[Software](https://wiki.gnome.org/Apps/Software) lets you install and update applications and system extensions.
A plugin system is used to access software from different sources.
Plugins are provided for:
 - Traditional package installation via PackageKit (e.g. Debian package, RPM).
 - Next generation packages: [Flatpak](https://flatpak.org/) and [Snap](https://snapcraft.io/).
 - Firmware updates.
 - Ratings and reviews ([ODRS](https://odrs.gnome.org/) and Ubuntu reviews).

Software supports showing metadata that closely matches the [AppStream](https://www.freedesktop.org/wiki/Distributions/AppStream/) format.

Software runs as a background service to provide update notifications and be a search provider for [GNOME Shell](https://wiki.gnome.org/Projects/GnomeShell).

# Building

Build locally with:
```
$ meson --prefix $PWD/install build/
$ ninja -C build/ all install
$ killall gnome-software
$ XDG_DATA_DIRS=install/share:$XDG_DATA_DIRS ./install/bin/gnome-software
```

# Debugging

Running with `--verbose` will give detailed logging information.
