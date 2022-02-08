Vendor customisation of GNOME Software
======================================

GNOME Software is in an unusual position in a distribution, as it lies at the
interface of the GNOME project and the distribution’s packaging and release
infrastructure. GNOME Software is the user interface which a lot of users will
use to see updates and new releases from their distribution. Distributions
understandably want to be able to put some of their own branding on this
interface, both to publicise their distribution and to confer some level of
official authority on the updates being provided.

For this reason, GNOME Software has a few ways which vendors can use to
customise its appearance.

A variety of different customisations have been implemented in the past, some of
which have been removed and others are still present. This document aims to
document the ones which are still present and supported. This document is *not
necessarily complete*. It will be added to over time as different customisations
are refreshed and updated.

If there is a supported customisation method which is not in this document,
please [submit a merge request](https://gitlab.gnome.org/GNOME/gnome-software/-/merge_requests/new)
to document it.

Likewise, if your distribution would like to customise gnome-software in a way
which isn’t currently supported, please
[open a new issue](https://gitlab.gnome.org/GNOME/gnome-software/-/issues/new?issue%5Bmilestone_id%5D=)
to discuss it. We don’t guarantee to implement anything, and customisations are
limited to adding branding in specific areas.

Principles
----------

The principles which guide vendor customisation features in GNOME Software are:
 * Avoid requiring vendor specific code.
   - Otherwise vendors have to maintain and test GNOME Software plugins, which
     is a lot of work.
 * Don’t use GSettings unless customisations really should be per-user.
   - While GSettings overrides are convenient, they are designed for user
     preferences, not packaging customisation.
 * Don’t require downstream patching of GNOME Software, although configure-time
   arguments are OK.
   - Many distributions are derived from other ones and would not like to have
     to maintain a packaging fork in order to make small customisations.
 * Be mindful of release cadences.
   - If customisations related to a new OS version were tied to the release
     cycle of GNOME Software, a new GNOME Software packaging release would have
     to be done by a distribution in advance of making their new OS release,
     which is a burden.
   - It’s easier to allow distributions to put customisations specific to a new
     OS version into a separate package.

Upgrade background image
------------------------

The background image which is shown when a new OS upgrade is available is
customisable in several ways. It’s displayed by the `GsUpgradeBanner` widget,
and shown on the updates page.

If your distribution has a specific GNOME Software plugin providing its upgrade
information, that plugin can provide CSS for rendering the background. See the
`fedora-pkgdb-collections` plugin for an example of this.

Otherwise, the background image is looked up from several well-known locations,
in order:
 * `${DATADIR}/gnome-software/backgrounds/${os_id}-${version}.png`
 * `${DATADIR}/gnome-software/backgrounds/${os_id}.png`

`${DATADIR}` is the configured data directory (typically `/usr/share`).
`${os_id}` is the `ID=` value from `/etc/os-release`, and `${version}` is the
version string being upgraded to.
