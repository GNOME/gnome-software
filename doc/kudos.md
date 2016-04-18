Kudos used in Software
======================

As part of the AppStream generation process we explode various parts of the
distro binary package and try to build metadata by merging various sources
together, for example AppData, desktop files and icons.

As part of this we also have access to the finished binary and libraries, and
so can also run tools on them to get a metric of awesomeness. So far, the
metrics of awesomeness (here-on known as "kudos") are:

 * `AppMenu` — has an application menu in line with the GNOME HIG
 * `HiDpiIcon` — installs a 128x128 or larger application icon
 * `HighContrast` — uses hicontrast or scalable icons for visually impaired users
 * `ModernToolkit` — uses a modern toolkit like Gtk-3 or QT-5
 * `Notifications` — registers desktop notifications
 * `SearchProvider` — provides a search provider for GNOME Shell or KDE Plasma
 * `UserDocs` — provides user documentation

These attempt to define how tightly the application is integrated with the
platform, which is usually a pretty good metric of awesomeness. Of course,
some applications like Blender are an island in terms of integration, but of
course are awesome.

There are some other "run-time" kudos used as well. These are not encoded by
the builder as they require some user information or are too specific to
GNOME Software. These include:

 * `FeaturedRecommended` — One of the design team chose to feature this
 * `HasKeywords` — there are keywords in the desktop file used for searching
 * `HasScreenshots` — more than one screenshot is supplied
 * `MyLanguage` — has a populated translation in my locale, or a locale fallback
 * `PerfectScreenshots` — screenshots are perfectly sized, in 16:9 aspect
 * `Popular` — lots of people have downloaded this (only available on Fedora)
 * `RecentRelease` — there been an upstream release in the last year

You can verify the kudos your application is getting by doing something like:

    killall gnome-software
    gnome-software --verbose

and then navigating to the details for an application you'll see on the console:

    id-kind:         desktop
    state:           available
    id:              blender.desktop
    kudo:            recent-release
    kudo:            featured-recommended
    kudo:            has-screenshots
    kudo:            popular

Manually Adding Kudos
---------------------

If the AppStream generator fails to detect a specific kudo then you can add them
manually in the AppData file and they will be used in the AppStream metadata.
To do this, just add something like:

    <kudos>
     <kudo>ModernToolkit</kudo>
     <kudo>UserDocs</kudo>
    </kudos>

Although, please bear in mind any application that is found cheating, i.e.
adding kudos artificially will have **all** the kudos manually removed
with a blacklist rule in the AppStream builder.

If you are a vendor, or a system distributor and just want to increase the
number of kudos for your pet proprietary application that's essential to
business function, a good kudo to manually add would be `FeaturedRecommended`,
although, perhaps adding the desktop ID to the GSettings key
`org.gnome.software.popular-overrides` would be a better idea.
Adding application IDs to this key allows you to show any business-critical
applications prominently in the GNOME Software application.
