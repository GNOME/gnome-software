# Copyright (C) 2010-2015 Richard Hughes <richard@hughsie.com>
AC_PREREQ(2.63)

AC_INIT([gnome-software],[3.22.2],[http://bugzilla.gnome.org/enter_bug.cgi?product=gnome-software])
AC_CONFIG_SRCDIR(src)
AM_INIT_AUTOMAKE([1.11 no-dist-gzip dist-xz tar-ustar serial-tests foreign])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# enable nice build output on automake1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_PROG_CC
AC_PROG_INSTALL
LT_INIT
AM_PROG_CC_C_O
IT_PROG_INTLTOOL([0.35.0])

# set up gtk-doc
GTK_DOC_CHECK([1.11],[--flavour no-tmpl])

dnl ---------------------------------------------------------------------------
dnl - Extra verbose warning switches
dnl ---------------------------------------------------------------------------
WARN_CFLAGS_EXTRA="
	-Waggregate-return
	-Warray-bounds
	-Wcast-align
	-Wclobbered
	-Wdeclaration-after-statement
	-Wempty-body
	-Wextra
	-Wformat=2
	-Wformat-nonliteral
	-Wformat-security
	-Wformat-signedness
	-Wignored-qualifiers
	-Wimplicit-function-declaration
	-Winit-self
	-Winline
	-Wmissing-declarations
	-Wmissing-format-attribute
	-Wmissing-include-dirs
	-Wmissing-noreturn
	-Wmissing-parameter-type
	-Wmissing-prototypes
	-Wnested-externs
	-Wno-discarded-qualifiers
	-Wno-missing-field-initializers
	-Wno-strict-aliasing
	-Wno-suggest-attribute=format
	-Wno-unused-parameter
	-Wold-style-definition
	-Woverride-init
	-Wpacked
	-Wpointer-arith
	-Wredundant-decls
	-Wreturn-type
	-Wshadow
	-Wsign-compare
	-Wstrict-aliasing
	-Wstrict-prototypes
	-Wswitch-default
	-Wtype-limits
	-Wundef
	-Wuninitialized
	-Wunused-but-set-variable
	-Wwrite-strings"
AX_APPEND_COMPILE_FLAGS([$WARN_CFLAGS_EXTRA], [WARN_CFLAGS])
AC_SUBST(WARN_CFLAGS)

dnl ---------------------------------------------------------------------------
dnl - gettext stuff
dnl ---------------------------------------------------------------------------
GETTEXT_PACKAGE=AC_PACKAGE_NAME
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE(GETTEXT_PACKAGE, "AC_PACKAGE_NAME", [gnome-software])

AM_PATH_GLIB_2_0(2.46.0, :, AC_MSG_ERROR([GLib is required]), gobject gmodule gio)
AM_GLIB_GNU_GETTEXT
GLIB_GSETTINGS

dnl ---------------------------------------------------------------------------
dnl - Check library dependencies
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(GTK, gtk+-3.0 >= 3.20.0 gio-unix-2.0)
PKG_CHECK_MODULES(APPSTREAM, appstream-glib >= 0.6.1)
PKG_CHECK_MODULES(GDK_PIXBUF, gdk-pixbuf-2.0 >= 2.31.5)
PKG_CHECK_MODULES(JSON_GLIB, json-glib-1.0 >= 1.1.1)
PKG_CHECK_MODULES(SQLITE, sqlite3)
PKG_CHECK_MODULES(SOUP, libsoup-2.4 >= 2.51.92)
PKG_CHECK_MODULES(GSETTINGS_DESKTOP_SCHEMAS, gsettings-desktop-schemas >= 3.11.5)
PKG_CHECK_MODULES(LIBSECRET, libsecret-1)
AC_PATH_PROG(APPSTREAM_UTIL, [appstream-util], [unfound])

# use -lm
LT_LIB_M
AC_SUBST(LIBM)

AC_ARG_ENABLE(man,
              [AS_HELP_STRING([--enable-man],
                              [generate man pages [default=auto]])],,
              enable_man=maybe)
AS_IF([test "$enable_man" != no], [
  AC_PATH_PROG([XSLTPROC], [xsltproc])
  AS_IF([test -z "$XSLTPROC"], [
    AS_IF([test "$enable_man" = yes], [
      AC_MSG_ERROR([xsltproc is required for --enable-man])
    ])
    enable_man=no
  ])
])
AM_CONDITIONAL(ENABLE_MAN, test "$enable_man" != no)

AC_ARG_ENABLE(dogtail,
              [AS_HELP_STRING([--enable-dogtail],
                              [test using dogtail [default=yes]])],,
              enable_dogtail=no)
AM_CONDITIONAL(ENABLE_DOGTAIL, test "$enable_dogtail" != no)

AC_ARG_ENABLE(tests,
              [AS_HELP_STRING([--enable-tests],
                              [test using tests [default=yes]])],,
              enable_tests=yes)
AM_CONDITIONAL(ENABLE_TESTS, test "$enable_tests" != no)

# spell checking
AC_ARG_ENABLE(gtkspell,
              [AS_HELP_STRING([--enable-gtkspell],
                              [enable gtkspell support [default=auto]])],,
              enable_gtkspell=yes)
AS_IF([test "x$enable_gtkspell" != "xno"], [
  PKG_CHECK_MODULES(GTKSPELL,
                    [gtkspell3-3.0],
                    [have_gtkspell=yes],
                    [have_gtkspell=no])
], [
    have_gtkspell=no
])
AS_IF([test "x$have_gtkspell" = "xyes"], [
    AC_DEFINE(HAVE_GTKSPELL,1,[Build gtkspell support])
], [
    AS_IF([test "x$enable_gtkspell" = "xyes"], [
          AC_MSG_ERROR([gtkspell support requested but 'gtkspell3' was not found])
    ])
])
AM_CONDITIONAL(HAVE_GTKSPELL, test "$have_gtkspell" != no)

# gnome-desktop support
AC_ARG_ENABLE(gnome-desktop,
              [AS_HELP_STRING([--enable-gnome-desktop],
                              [enable gnome-desktop support [default=auto]])],,
              enable_gnome_desktop=yes)
AS_IF([test "x$enable_gnome_desktop" != "xno"], [
  PKG_CHECK_MODULES(GNOME_DESKTOP,
                    [gnome-desktop-3.0 >= 3.17.92],
                    [have_gnome_desktop=yes],
                    [have_gnome_desktop=no])
], [
    have_gnome_desktop=no
])
AS_IF([test "x$have_gnome_desktop" = "xyes"], [
    AC_DEFINE(HAVE_GNOME_DESKTOP,1,[Build gnome_desktop support])
], [
    AS_IF([test "x$enable_gnome_desktop" = "xyes"], [
          AC_MSG_ERROR([gnome-desktop support requested but 'gnome-desktop' was not found])
    ])
])
AM_CONDITIONAL(HAVE_GNOME_DESKTOP, test "$have_gnome_desktop" != no)

# PackageKit
AC_ARG_ENABLE(packagekit,
              [AS_HELP_STRING([--enable-packagekit],
                              [enable PackageKit support [default=auto]])],,
              enable_packagekit=maybe)
AS_IF([test "x$enable_packagekit" != "xno"], [
    PKG_CHECK_MODULES(PACKAGEKIT,
                      [packagekit-glib2 >= 1.1.0],
                      [have_packagekit=yes],
                      [have_packagekit=no])
], [
    have_packagekit=no
])
AS_IF([test "x$have_packagekit" = "xyes"], [
    AC_DEFINE(HAVE_PACKAGEKIT,1,[Build packagekit support])
], [
    AS_IF([test "x$enable_packagekit" = "xyes"], [
          AC_MSG_ERROR([packagekit support requested but 'packagekit-glib2' was not found])
    ])
])
AM_CONDITIONAL(HAVE_PACKAGEKIT, test "$have_packagekit" != no)

# PolicyKit
AC_ARG_ENABLE(polkit,
              [AS_HELP_STRING([--enable-polkit],
                              [enable PolicyKit support [default=auto]])],,
              enable_polkit=maybe)

AS_IF([test "x$enable_polkit" != "xno"], [
    PKG_CHECK_MODULES(POLKIT,
                      [polkit-gobject-1],
                      [have_polkit=yes],
                      [have_polkit=no])
], [
    have_polkit=no
])
AS_IF([test "x$have_polkit" = "xyes"], [
    AC_DEFINE(HAVE_POLKIT,1,[Build polkit support])
], [
    AS_IF([test "x$enable_polkit" = "xyes"], [
          AC_MSG_ERROR([polkit support requested but 'polkit-gobject-1' was not found])
    ])
])
AM_CONDITIONAL(HAVE_POLKIT, test "$have_polkit" != no)

# fwupd
AC_ARG_ENABLE(firmware,
              [AS_HELP_STRING([--enable-firmware],
                              [enable firmware support [default=auto]])],,
              enable_firmware=maybe)
AS_IF([test "x$enable_firmware" != "xno"], [
    PKG_CHECK_MODULES(FWUPD,
                      [fwupd >= 0.7.0],
                      [have_firmware=yes],
                      [have_firmware=no])
], [
    have_firmware=no
])
AS_IF([test "x$have_firmware" = "xyes"], [
    AC_DEFINE(HAVE_FIRMWARE,1,[Build firmware support])
], [
    AS_IF([test "x$enable_firmware" = "xyes"], [
          AC_MSG_ERROR([Firmware support requested but 'fwupd' was not found])
    ])
])
AM_CONDITIONAL(HAVE_FIRMWARE, test x$have_firmware = xyes)

# flatpak
AC_ARG_ENABLE(flatpak,
              [AS_HELP_STRING([--enable-flatpak],
                              [enable flatpak support [default=auto]])],,
              enable_flatpak=maybe)
AS_IF([test "x$enable_flatpak" != "xno"], [
    PKG_CHECK_MODULES(FLATPAK,
                      [flatpak >= 0.4.14],
                      [have_flatpak=yes],
                      [have_flatpak=no])
], [
    have_flatpak=no
])
AS_IF([test "x$have_flatpak" = "xyes"], [
    AC_DEFINE(HAVE_FLATPAK,1,[Build flatpak support])
], [
    AS_IF([test "x$enable_flatpak" = "xyes"], [
          AC_MSG_ERROR([flatpak support requested but 'flatpak' was not found])
    ])
])
AM_CONDITIONAL(HAVE_FLATPAK, test "$have_flatpak" != no)

# ostree
AC_ARG_ENABLE(ostree,
              [AS_HELP_STRING([--enable-ostree],
                              [enable ostree support [default=auto]])],,
              enable_ostree=maybe)
AS_IF([test "x$enable_ostree" != "xno"], [
    PKG_CHECK_MODULES(OSTREE,
                      [ostree-1],
                      [have_ostree=yes],
                      [have_ostree=no])
], [
    have_ostree=no
])
AS_IF([test "x$have_ostree" = "xyes"], [
    AC_DEFINE(HAVE_OSTREE,1,[Build ostree support])
], [
    AS_IF([test "x$enable_ostree" = "xyes"], [
          AC_MSG_ERROR([ostree support requested but 'ostree' was not found])
    ])
])
AM_CONDITIONAL(HAVE_OSTREE, test "$have_ostree" != no)

# Limba
AC_ARG_ENABLE(limba,
              [AS_HELP_STRING([--enable-limba],
                              [enable Limba support [default=auto]])],,
              enable_limba=maybe)
AS_IF([test "x$enable_limba" != "xno"], [
    PKG_CHECK_MODULES(LIMBA,
                      limba >= 0.5.6,
                      [have_limba=yes],
                      [have_limba=no])
], [
    have_limba=no
])
AS_IF([test "x$have_limba" = "xyes"], [
    AC_DEFINE(HAVE_LIMBA, 1, [Build Limba support])
], [
    AS_IF([test "x$enable_limba" = "xyes"], [
          AC_MSG_ERROR([Limba support requested but 'limba>=0.5.6' was not found])
    ])
])
AM_CONDITIONAL(HAVE_LIMBA, test x$enable_limba = xyes)

# RPM
AC_ARG_ENABLE(rpm,
              [AS_HELP_STRING([--enable-rpm],
                              [enable RPM support [default=auto]])],,
              enable_rpm=maybe)
AS_IF([test "x$enable_rpm" != "xno"], [
    PKG_CHECK_MODULES(RPM,
                      rpm,
                      [have_rpm=yes],
                      [have_rpm=no])
], [
    have_rpm=no
])
AS_IF([test "x$have_rpm" = "xyes"], [
    AC_DEFINE(HAVE_RPM, 1, [Build RPM support])
], [
    AS_IF([test "x$enable_rpm" = "xyes"], [
          AC_MSG_ERROR([RPM support requested but 'rpm' was not found])
    ])
])
AM_CONDITIONAL(HAVE_RPM, test x$have_rpm = xyes)

# Steam
AC_ARG_ENABLE(steam,
              [AS_HELP_STRING([--enable-steam],
                              [enable steam support [default=yes]])],,
              enable_steam=yes)
AS_IF([test "x$enable_steam" = "xyes"], [
    AC_DEFINE(HAVE_STEAM,1,[Build steam support])
])
AM_CONDITIONAL(HAVE_STEAM, test x$enable_steam = xyes)

# Shell Extensions
AC_ARG_ENABLE(shell_extensions,
              [AS_HELP_STRING([--enable-shell-extensions],
                              [enable GNOME shell extensions support [default=yes]])],,
              enable_shell_extensions=yes)
AS_IF([test "x$enable_shell_extensions" = "xyes"], [
  AC_DEFINE(HAVE_SHELL_EXTENSIONS,1,[Build GNOME Shell extensions])], [
])
AM_CONDITIONAL(HAVE_SHELL_EXTENSIONS, [test "x$enable_shell_extensions" = "xyes"])

# Open Desktop Ratings Service
AC_ARG_ENABLE(odrs,
              [AS_HELP_STRING([--enable-odrs],
                              [enable ODRS support [default=yes]])],,
              enable_odrs=yes)
AM_CONDITIONAL(HAVE_ODRS, test "x$enable_odrs" = "xyes")

# Ubuntu-specific Reviews
AC_ARG_ENABLE(ubuntu-reviews,
              [AS_HELP_STRING([--enable-ubuntu-reviews],
                              [enable Ubuntu reviews [default=no]])],,
              enable_ubuntu_reviews=no)
AS_IF([test "x$enable_ubuntu_reviews" = "xyes"], [
    PKG_CHECK_MODULES(OAUTH, oauth)
])
AM_CONDITIONAL(HAVE_UBUNTU_REVIEWS, [test "x$enable_ubuntu_reviews" = "xyes"])
AC_DEFINE(HAVE_UBUNTU_REVIEWS,1,[Build Ubuntu reviews])

# Webapps
AC_ARG_ENABLE(webapps,
              [AS_HELP_STRING([--enable-webapps],
                              [enable webapps [default=yes]])],,
              enable_webapps=yes)
AM_CONDITIONAL(HAVE_WEBAPPS, [test "x$enable_webapps" = "xyes"])

# GUDev
AC_ARG_ENABLE(gudev,
              [AS_HELP_STRING([--enable-gudev],
                              [enable GUDev support [default=auto]])],,
              enable_gudev=maybe)
AS_IF([test "x$enable_gudev" != "xno"], [
    PKG_CHECK_MODULES(GUDEV,
                      gudev-1.0,
                      [have_gudev=yes],
                      [have_gudev=no])
], [
    have_gudev=no
])
AS_IF([test "x$have_gudev" = "xyes"], [
    AC_DEFINE(HAVE_GUDEV, 1, [Build GUDev support])
], [
    AS_IF([test "x$enable_gudev" = "xyes"], [
          AC_MSG_ERROR([GUDEV support requested but 'gudev-1.0' was not found])
    ])
])
AM_CONDITIONAL(HAVE_GUDEV, test x$have_gudev = xyes)

# this refers to the gnome-software plugin API version
# this is not in any way related to a package or soname version
GS_PLUGIN_API_VERSION=11
AC_SUBST(GS_PLUGIN_API_VERSION)
AC_DEFINE_UNQUOTED([GS_PLUGIN_API_VERSION], "$GS_PLUGIN_API_VERSION", [the plugin API version])

GLIB_TESTS

dnl ---------------------------------------------------------------------------
dnl - Makefiles, etc.
dnl ---------------------------------------------------------------------------
AC_CONFIG_FILES([
Makefile
po/Makefile.in
data/appdata/Makefile
data/Makefile
data/icons/Makefile
data/icons/hicolor/Makefile
data/icons/hicolor/16x16/Makefile
data/icons/hicolor/22x22/Makefile
data/icons/hicolor/24x24/Makefile
data/icons/hicolor/32x32/Makefile
data/icons/hicolor/48x48/Makefile
data/icons/hicolor/256x256/Makefile
data/icons/hicolor/scalable/Makefile
data/tests/Makefile
doc/Makefile
doc/api/Makefile
src/gnome-software.pc
src/Makefile
src/plugins/Makefile
tests/Makefile
])
AC_OUTPUT

dnl ==========================================================================
echo "
                    gnome-software $VERSION
                  ==========================

        prefix:                    ${prefix}
        datadir:                   ${datadir}
        compiler:                  ${CC}
        cflags:                    ${CFLAGS}
        cppflags:                  ${CPPFLAGS}
        Dogtail:                   ${enable_dogtail}
        Self tests:                ${enable_tests}
        PackageKit support:        ${have_packagekit}
        PolicyKit support:         ${have_polkit}
        Firmware support:          ${have_firmware}
        Limba support:             ${have_limba}
        Flatpak support:           ${have_flatpak}
        OSTree support:            ${have_ostree}
        GtkSpell support:          ${have_gtkspell}
        GNOME desktop support:     ${have_gnome_desktop}
        RPM support:               ${have_rpm}
        GUDev support:             ${have_gudev}
        Steam support:             ${enable_steam}
        GNOME Shell ext. support:  ${enable_shell_extensions}
        ODRS support:              ${enable_odrs}
        Webapps support:           ${enable_webapps}
        Ubuntu Reviews support:    ${enable_ubuntu_reviews}
"
