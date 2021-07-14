GNOME Software Release Notes
===

Release schedule
---

GNOME Software releases are done on the timetable set by the [GNOME release schedule](https://wiki.gnome.org/Schedule).

Maintainers take it in turns to make releases so that the load is spread out evenly.

Making a release
---

Adapted from the [GNOME release process](https://wiki.gnome.org/MaintainersCorner/Releasing).

These instructions use the following variables:
 - `new_version`: the version number of the release you are making, for example 3.38.1
 - `previous_version`: the version number of the most-recently released version in the same release series, for example 3.38.0
 - `branch`: the branch which the release is based on, for example gnome-40 or main
 - `key_id`: the ID of your GPG key, see the output of `gpg --list-keys` and the note at the end of this file

Make sure your repository is up to date and doesn’t contain local changes:
```
git pull
git status
```

Check the version in `meson.build` is correct for this release.

Download
[gitlab-changelog](https://gitlab.gnome.org/pwithnall/gitlab-changelog) and use
it to write release entries:
```
gitlab-changelog.py GNOME/gnome-software ${previous_version}..
```

Edit this down to just the user visible changes, and list them in
`data/appdata/org.gnome.Software.appdata.xml.in`. User visible changes are ones
which the average user might be interested to know about, such as a fix for an
impactful bug, a UI change, or a feature change.

You can get review of your appdata changes from other co-maintainers if you wish.

Generate `NEWS` file:
```
appstreamcli metainfo-to-news ./data/appdata/org.gnome.Software.appdata.xml.in ./NEWS
```

Commit the release:
```
git add -p
git commit -m "Release version ${new_version}"
```

Build the release tarball:
```
# Only execute git clean if you don't have anything not tracked by git that you
# want to keep
git clean -dfx
meson --prefix $PWD/install -Dbuildtype=release build/
ninja -C build/ dist
```

Tag, sign and push the release (see below for information about `git evtag`):
```
git evtag sign -u ${key_id} ${new_version}
git push --atomic origin ${branch} ${new_version}
```
To use a specific key add an option `-u ${keyid|email}` after the `sign` argument.

Use `Tag ${new_version} release` as the tag message.

Upload the release tarball:
```
scp build/meson-dist/gnome-software-${new_version}.tar.xz master.gnome.org:
ssh master.gnome.org ftpadmin install gnome-software-${new_version}.tar.xz
```

Add the release notes to GitLab and close the milestone:
 - Go to https://gitlab.gnome.org/GNOME/gnome-software/-/tags/${new_version}/release/edit
   and upload the release notes for the new release from the `NEWS` file
 - Go to https://gitlab.gnome.org/GNOME/gnome-software/-/releases/${new_version}/edit
   and link the milestone to it, then list the new release tarball and
   `sha256sum` file in the ‘Release Assets’ section as the ’Other’ types.
   Get the file links from https://download.gnome.org/sources/gnome-software/ and
   name them ’Release tarball’ and ’Release tarball sha256sum’
 - Go to https://gitlab.gnome.org/GNOME/gnome-software/-/milestones/
   choose the milestone and close it, as all issues and merge requests tagged
   for this release should now be complete

Post release version bump in `meson.build`:
```
# edit meson.build, then
git commit -a -m "trivial: Post release version bump"
git push
```

`git-evtag`
---

Releases should be done with `git evtag` rather than `git tag`, as it provides
stronger security guarantees. See
[its documentation](https://github.com/cgwalters/git-evtag) for more details.
In particular, it calculates its checksum over all blobs reachable from the tag,
including submodules; and uses a stronger checksum than SHA-1.

You will need a GPG key for this, ideally which has been signed by others so
that it can be verified as being yours. However, even if your GPG key is
unsigned, using `git evtag` is still beneficial over using `git tag`.
