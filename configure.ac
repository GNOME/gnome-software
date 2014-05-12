# Copyright (C) 2010-2012 Richard Hughes <richard@hughsie.com>
AC_PREREQ(2.63)

AC_INIT([gnome-software],[3.12.3],[http://bugzilla.gnome.org/enter_bug.cgi?product=gnome-software])
AC_CONFIG_SRCDIR(src)
AM_INIT_AUTOMAKE([1.11 no-dist-gzip dist-xz tar-ustar serial-tests])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# enable nice build output on automake1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_RANLIB
LT_INIT
AM_PROG_CC_C_O
IT_PROG_INTLTOOL([0.35.0])
#YELP_HELP_INIT

#GNOME_COMPILE_WARNINGS(error)
#GNOME_DOC_INIT

dnl ---------------------------------------------------------------------------
dnl - Extra verbose warning switches
dnl ---------------------------------------------------------------------------

if test "$GCC" = "yes"; then
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wall"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wcast-align -Wno-uninitialized"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-declarations"
#	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wredundant-decls"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wpointer-arith"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wcast-align"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wwrite-strings"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Winit-self"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wreturn-type"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wformat-nonliteral"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wformat-security"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-include-dirs"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-format-attribute"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wclobbered"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wempty-body"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wignored-qualifiers"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wsign-compare"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wtype-limits"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wuninitialized"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Waggregate-return"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wdeclaration-after-statement"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wshadow"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wno-strict-aliasing"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Winline"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-parameter-type"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Woverride-init"
else
	WARNINGFLAGS_C=""
fi
AC_SUBST(WARNINGFLAGS_C)

dnl ---------------------------------------------------------------------------
dnl - gettext stuff
dnl ---------------------------------------------------------------------------
GETTEXT_PACKAGE=AC_PACKAGE_NAME
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE(GETTEXT_PACKAGE, "AC_PACKAGE_NAME", [gnome-software])

AM_PATH_GLIB_2_0(2.39.1, :, AC_MSG_ERROR([GLib is required]), gobject gmodule gio)
AM_GLIB_GNU_GETTEXT
GLIB_GSETTINGS

dnl ---------------------------------------------------------------------------
dnl - Check library dependencies
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(GTK, gtk+-3.0 >= 3.11.5 gio-unix-2.0)
PKG_CHECK_MODULES(PACKAGEKIT, packagekit-glib2 >= 0.8.12)
PKG_CHECK_MODULES(SQLITE, sqlite3)
PKG_CHECK_MODULES(SOUP, libsoup-2.4)
PKG_CHECK_MODULES(GSETTINGS_DESKTOP_SCHEMAS, gsettings-desktop-schemas >= 3.11.5)
PKG_CHECK_MODULES(GNOME_DESKTOP, gnome-desktop-3.0)
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

# this refers to the gnome-software plugin API version
# this is not in any way related to a package or soname version
GS_PLUGIN_API_VERSION=3
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
data/icons/16x16/Makefile
data/icons/22x22/Makefile
data/icons/24x24/Makefile
data/icons/32x32/Makefile
data/icons/48x48/Makefile
data/icons/256x256/Makefile
data/icons/scalable/Makefile
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
"

