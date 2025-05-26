GNOME Software out of tree plugins
==================================

GNOME Software supports out of tree plugins, provided by third parties. These
plugins can link against the libgnomesoftware library, as it’s installed
publicly on systems which have GNOME Software.

Note that the API of libgnomesoftware can change between versions. Plugins must
be installed in a versioned subdirectory which matches the API version they’re
built against (for example, `${libdir}/gnome-software/plugins-${api_version}/`).
GNOME Software will only load plugins for the single API version it provides, so
plugins must be kept up to date with API changes if they are to be used.

Plugins can currently only be written in C, but there are [plans to make
libgnomesoftware introspectable](https://gitlab.gnome.org/GNOME/gnome-software/-/merge_requests/2182)
which would allow plugins to be written in other languages.

Known out of tree plugins
---

This list is not exhaustive.

 - https://github.com/FuriLabs/gnome-software-plugin-android
 - https://github.com/Cogitri/gnome-software-plugin-apk
