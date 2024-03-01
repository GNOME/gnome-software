[![Build Status](https://gitlab.gnome.org/GNOME/gnome-software/badges/main/pipeline.svg)](https://gitlab.gnome.org/GNOME/gnome-software/pipelines)

# Software

[Software](https://apps.gnome.org/Software) allows users to easily find,
discover and install apps. It also keeps their OS, apps and devices up to date
without them having to think about it, and gives them confidence that their
system is up to date. It supports popular distributions, subject to those
distributions maintaining their own distro-specific integration code.

The specific use cases that Software covers are [documented in more detail](./doc/use-cases.md).

# Features

A plugin system is used to access software from different sources.
Plugins are provided for:
 - Traditional package installation via PackageKit (e.g. Debian package, RPM).
 - Next generation packages: [Flatpak](https://flatpak.org/) and [Snap](https://snapcraft.io/).
 - Firmware updates.
 - Ratings and reviews using [ODRS](https://odrs.gnome.org/).

Software supports showing metadata that closely matches the [AppStream](https://www.freedesktop.org/wiki/Distributions/AppStream/) format.

Software runs as a background service to provide update notifications and be a search provider for [GNOME Shell](https://gitlab.gnome.org/GNOME/gnome-shell/).

# Contact

For questions about how to use Software, ask on [Discourse](https://discourse.gnome.org/tag/gnome-software).

Bug reports and merge requests should be filed on [GNOME GitLab](https://gitlab.gnome.org/GNOME/gnome-software).

For development discussion, join us on `#gnome-software:gnome.org` on [Matrix](https://matrix.to/#/#gnome-software:gnome.org).

# Documentation for app developers and vendors

Specific documentation is available for app developers who wish to test
how their apps appear in GNOME Software; and for distribution vendors
who wish to customise how GNOME Software appears in their distribution:
 * [Tools in GNOME Software for app developers](./doc/app-developers.md)
 * [Vendor customisation of GNOME Software](./doc/vendor-customisation.md)

# Running a nightly build

A [flatpak bundle](https://docs.flatpak.org/en/latest/single-file-bundles.html)
of Software can be built on demand here by running the ‘flatpak bundle’ CI job.
It is not fully functional, but is useful for development and testing of
upcoming UI changes to Software. It may become more functional over time. It
is not an official or supported release.

The CI job saves the bundle in its artifacts list as `gnome-software-dev.flatpak`.
This can be installed and run locally by downloading it and running:
```
$ flatpak install --bundle ./gnome-software-dev.flatpak
$ flatpak run org.gnome.SoftwareDevel
```

# Building locally

Software uses a number of plugins and depending on your operating system you
may want to disable or enable certain ones. For example on Fedora Silverblue
you'd want to disable the packagekit plugin as it wouldn't work. See the list
in `meson_options.txt` and use e.g. `-Dpackagekit=false` in the `meson` command
below.

Build locally with:
```
$ meson --prefix $PWD/install build/
$ ninja -C build/ all install
$ killall gnome-software
# On Fedora, RHEL, etc:
$ XDG_DATA_DIRS=install/share:$XDG_DATA_DIRS LD_LIBRARY_PATH=install/lib64/:$LD_LIBRARY_PATH ./install/bin/gnome-software
# On Debian, Ubuntu, etc:
$ XDG_DATA_DIRS=install/share:$XDG_DATA_DIRS LD_LIBRARY_PATH=install/lib/x86_64-linux-gnu/:$LD_LIBRARY_PATH ./install/bin/gnome-software
```

# Debugging

See [the debugging documentation](./doc/debugging.md).

# Maintainers

Software is maintained by several co-maintainers, as listed in `gnome-software.doap`.
All changes to Software need to be reviewed by at least one co-maintainer (who
can’t review their own changes). Larger decisions need input from at least two
co-maintainers.
