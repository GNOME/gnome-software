# Copyright (C) 2010-2015 Richard Hughes <richard@hughsie.com>
AC_PREREQ(2.63)

AC_INIT([gnome-software],[3.19.5],[http://bugzilla.gnome.org/enter_bug.cgi?product=gnome-software])
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
#YELP_HELP_INIT

#GNOME_DOC_INIT

dnl ---------------------------------------------------------------------------
dnl - Extra verbose warning switches
dnl ---------------------------------------------------------------------------

GNOME_COMPILE_WARNINGS([maximum],[
	-Wmissing-declarations
	-Wcast-align
	-Wwrite-strings
	-Wreturn-type
	-Wformat-nonliteral
	-Wmissing-format-attribute
	-Wclobbered
	-Wempty-body
	-Wignored-qualifiers
	-Wsign-compare
	-Wtype-limits
	-Wuninitialized
	-Waggregate-return
	-Wdeclaration-after-statement
	-Wshadow
	-Wno-strict-aliasing
	-Winline
	-Wmissing-parameter-type
	-Woverride-init
])

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
PKG_CHECK_MODULES(GTK, gtk+-3.0 >= 3.18.2 gio-unix-2.0)
PKG_CHECK_MODULES(APPSTREAM, appstream-glib >= 0.5.5)
PKG_CHECK_MODULES(JSON_GLIB, json-glib-1.0)
PKG_CHECK_MODULES(SQLITE, sqlite3)
PKG_CHECK_MODULES(SOUP, libsoup-2.4 >= 2.51.92)
PKG_CHECK_MODULES(GSETTINGS_DESKTOP_SCHEMAS, gsettings-desktop-schemas >= 3.11.5)
PKG_CHECK_MODULES(GNOME_DESKTOP, gnome-desktop-3.0 >= 3.17.92)
AC_PATH_PROG(APPSTREAM_UTIL, [appstream-util], [unfound])
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
              enable_dogtail=yes)
AM_CONDITIONAL(ENABLE_DOGTAIL, test "$enable_dogtail" != no)

# PackageKit
AC_ARG_ENABLE(packagekit,
              [AS_HELP_STRING([--enable-packagekit],
                              [enable PackageKit support [default=auto]])],,
              enable_packagekit=maybe)
AS_IF([test "x$enable_packagekit" != "xno"], [
    PKG_CHECK_MODULES(PACKAGEKIT,
                      [packagekit-glib2 >= 1.0.11],
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
                      [fwupd],
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

# xdg-app
AC_ARG_ENABLE(xdg-app,
              [AS_HELP_STRING([--enable-xdg-app],
                              [enable xdg-app support [default=auto]])],,
              enable_xdg_app=maybe)
AS_IF([test "x$enable_xdg_app" != "xno"], [
    PKG_CHECK_MODULES(XDG_APP,
                      [xdg-app >= 0.4.7],
                      [have_xdg_app=yes],
                      [have_xdg_app=no])
], [
    have_xdg_app=no
])
AS_IF([test "x$have_xdg_app" = "xyes"], [
    AC_DEFINE(HAVE_XDG_APP,1,[Build xdg-app support])
], [
    AS_IF([test "x$enable_xdg_app" = "xyes"], [
          AC_MSG_ERROR([xdg-app support requested but 'xdg-app' was not found])
    ])
])
AM_CONDITIONAL(HAVE_XDG_APP, test "$have_xdg_app" != no)

# Limba
AC_ARG_ENABLE(limba,
              [AS_HELP_STRING([--enable-limba],
                              [enable Limba support [default=auto]])],,
              enable_limba=maybe)
AS_IF([test "x$enable_limba" != "xno"], [
    PKG_CHECK_MODULES(LIMBA,
                      limba >= 0.4.2,
                      [have_limba=yes],
                      [have_limba=no])
], [
    have_limba=no
])
AS_IF([test "x$have_limba" = "xyes"], [
    AC_DEFINE(HAVE_LIMBA, 1, [Build Limba support])
], [
    AS_IF([test "x$enable_limba" = "xyes"], [
          AC_MSG_ERROR([Limba support requested but 'limba>=0.4.2' was not found])
    ])
])
AM_CONDITIONAL(HAVE_LIMBA, test x$enable_limba = xyes)

# this refers to the gnome-software plugin API version
# this is not in any way related to a package or soname version
GS_PLUGIN_API_VERSION=8
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
data/featured.ini
data/Makefile
data/modulesets/Makefile
data/icons/Makefile
data/icons/hicolor/Makefile
data/icons/hicolor/16x16/Makefile
data/icons/hicolor/22x22/Makefile
data/icons/hicolor/24x24/Makefile
data/icons/hicolor/32x32/Makefile
data/icons/hicolor/48x48/Makefile
data/icons/hicolor/256x256/Makefile
data/icons/hicolor/scalable/Makefile
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
        PackageKit support:        ${have_packagekit}
        PolicyKit support:         ${have_polkit}
        Firmware support:          ${have_firmware}
        XDG-APP support:           ${have_xdg_app}
"

