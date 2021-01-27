GNOME Software Release Notes
===

Release schedule
---

GNOME Software releases are done on the timetable set by the [GNOME release schedule](https://wiki.gnome.org/Schedule).

Maintainers take it in turns to make releases so that the load is spread out evenly.

Making a release
---

Adapted from the [GNOME release process](https://wiki.gnome.org/MaintainersCorner/Releasing).

Make sure your repository is up to date and doesn’t contain local changes:
```
git pull
git status
```

Check the version in `meson.build` is correct for this release.

Write release entries:
```
git log --format="%s" --cherry-pick --right-only 3.37.92... | grep -i -v trivial | grep -v Merge | sort | uniq
```

Add any user visible changes into `data/appdata/org.gnome.Software.appdata.xml.in`.

Generate `NEWS` file:
```
appstream-util appdata-to-news ../data/appdata/org.gnome.Software.appdata.xml.in > NEWS
```

Tag the release:
```
git add -p
git commit -m "Release version 3.38.1"
git evtag sign 3.38.1
```

Build the release tarball:
```
ninja dist
git push --tags
git push
```

Upload the release tarball:
```
scp meson-dist/*.tar.xz rhughes@master.gnome.org:
ssh rhughes@master.gnome.org
ftpadmin install gnome-software-*.tar.xz
```

Post release version bump in `meson.build`
```
git commit -a -m "trivial: Post release version bump"
git push
```
