GNOME Software Release Notes
===

Making a release
---

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
git commit -a -m "Release version 3.38.1"
git tag -s 3.38.1 -m "==== Version 3.38.1 ===="
<enter password>
```

Build the release tarball:
```
ninja-build dist
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
