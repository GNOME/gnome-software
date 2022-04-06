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

Featured apps and Editor’s Choice
---------------------------------

There are several ways to promote and highlight specific applications in GNOME
Software. On the overview page, there’s a carousel of featured applications
(`featured_carousel`), and an “Editor’s Choice” section (`box_popular`). Both of
them highlight curated sets of applications. The same is true on each category
page: a carousel (`top_carousel`) and an “Editor’s Choice” section
(`featured_flow_box`) are present.

Both pages also have a “New & Updated” section (`box_recent` or
`recently_updated_flow_box`) presented below “Editor’s Choice”. The applications
listed in the new and updated section are not curated: they are chosen as the
applications which have had a recent release, according to the
`component/releases/release[@timestamp]` attribute in their metainfo.
Technically these are the results of the `gs_plugin_add_recent()` vfunc.

Applications are included in any of the curated sets through having special
metadata in their metainfo. The required metadata is different for the different
sections:
 * Carousel on the overview page: Applications are included if they have
   `component/custom/value[@key='GnomeSoftware::FeatureTile]` or
   `component/custom/value[@key='GnomeSoftware::FeatureTile-css]` set in their
   metainfo. They are also required to have a high-resolution icon, and the set
   of applications shown in the carousel is randomised and limited to (for
   example) 5. Technically these are the results of the
   `gs_plugin_add_featured()` vfunc.
 * “Editor’s Choice” on the overview page: Applications are included if they
   have `component/kudos/kudo[text()='GnomeSoftware::popular']` set in their
   metainfo. Technically these are the results of the `gs_plugin_add_popular()`
   vfunc.
 * Carousel on the category page: Applications are included if they are in the
   `Featured` subcategory of the displayed category. They are also required to
   have a high-resolution icon, and the set of applications shown in the carousel
   is randomised and limited to (for example) 5.
 * “Editor’s Choice” on the category page: Applications are included if they
   meet the requirements for being in the carousel, but weren’t chosen as part
   of the randomisation process.

Example:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<components>
  <component merge="append">
    <!-- The ID must always be present to allow merging -->
    <id>org.gnome.Podcasts</id>

    <!-- Make the app a candidate for inclusion in the carousel on the
         overview page (if it has a hi-res icon). -->
    <custom>
      <value key="GnomeSoftware::FeatureTile">True</value>
    </custom>

    <!-- Include the app in the “Editor’s Choice” section on the overview page. -->
    <kudos>
      <kudo>GnomeSoftware::popular</kudo>
    </kudos>

    <!-- Make the app a candidate for inclusion in the carousel or
         “Editor’s Choice” section on category pages (if it has a hi-res icon). -->
    <categories>
      <!-- Note that, due to a bug (https://gitlab.gnome.org/GNOME/gnome-software/-/issues/1649),
           currently all the other categories for the app must also be listed
           here, as well as the additional ‘Featured’ category. -->
      <category>AudioVideo</category>
      <category>Player</category>
      <!-- This category has been added: -->
      <category>Featured</category>
    </categories>
  </component>
  <!-- more components -->
</components>
```

There are several ways to modify the metainfo for applications so that they are
highlighted as required, all of which involve providing an additional appstream
file which sets the additional metainfo for those applications.

The main approach is to ship an additional distro-specific appstream file in
`${DATADIR}/swcatalog/xml`, providing and updating it via normal distribution
channels. For example, by packaging it in its own package which is updated
regularly.

For distributions which can’t do regular updates of individual files – such as
image-based distributions – GNOME Software can download distro-specific
appstream files from the internet. List them in the `external-appstream-urls`
GSetting in `/org/gnome/software`, typically via a distribution-provided
GSettings override. Each URL must be HTTPS, and must point to a valid appstream
file. GNOME Software must be configured with `-Dexternal_appstream=true` for
this to work.

GNOME Software will periodically check and download any updates to these
files, and will cache them locally. Ensure the `If-Modified-Since` HTTP header
functions correctly on your server, or GNOME Software’s caching will be
ineffective.

The `external-appstream-urls` mechanism may change in future.

GNOME Software ships a default list of featured applications, chosen to match
the [GNOME Circle](https://circle.gnome.org/). See
`data/assets/org.gnome.Software.Featured.xml` for this list, and for an example
of the metainfo XML needed to feature or highlight applications. See
`data/assets/org.gnome.Software.Popular.xml` for a default hard-coded list of
popular applications, which is displayed in the “Editor’s Choice” section of the
overview page.

Pass `-Ddefault_featured_apps=false` when configuring GNOME Software to disable
the default list of featured applications. Pass `-Dhardcoded_popular=false` to
disable the default list of “Editor’s Choice” applications.
