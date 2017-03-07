## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

# core
SUBDIRS = core

# non-core, but no configure option required
SUBDIRS += dpkg
SUBDIRS += dummy
SUBDIRS += fedora-distro-upgrades
SUBDIRS += fedora-tagger-usage
SUBDIRS += ubuntuone
SUBDIRS += repos

if HAVE_LIMBA
SUBDIRS += limba
endif

if HAVE_ODRS
SUBDIRS += odrs
endif

if HAVE_STEAM
SUBDIRS += steam
endif

if HAVE_UBUNTU_REVIEWS
SUBDIRS += ubuntu-reviews
endif

if HAVE_OSTREE
SUBDIRS += ostree
endif

if HAVE_EXTERNAL_APPSTREAM
SUBDIRS += external-appstream
endif

if HAVE_SNAP
SUBDIRS += snap
endif

if HAVE_FIRMWARE
SUBDIRS += fwupd
endif

if HAVE_PACKAGEKIT
SUBDIRS += packagekit
endif

if HAVE_GUDEV
SUBDIRS += modalias
endif

if HAVE_WEBAPPS
SUBDIRS += epiphany
endif

if HAVE_SHELL_EXTENSIONS
SUBDIRS += shell-extensions
endif

if HAVE_FLATPAK
SUBDIRS += flatpak
endif

-include $(top_srcdir)/git.mk
